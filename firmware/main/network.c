// PrivacySense Matter Room Hub - network.c
//
// Wi-Fi Station management with proper lifecycle:
//   - On boot: if ESP-IDF Wi-Fi NVS has saved credentials, start STA and
//     connect automatically.
//   - On disconnect: schedule a reconnect via xTimer (async, non-blocking)
//     with exponential backoff capped at 60 s. Auth failures (bad passphrase)
//     stop after 3 attempts and report "needs re-commissioning".
//   - On reconnect success: backoff counter reset to 0.
//
// Event flow (task-architecture.md §3, §5.1, §6.1):
//   ESP-IDF Wi-Fi/IP event loop (event-loop task context)
//     → on_wifi_event / on_ip_event update internal state and enqueue a
//       status into s_status_queue (xQueueOverwrite, non-blocking)
//   network_task
//     → blocks on s_status_queue with 2 s timeout (TWDT feed)
//     → on wake, builds app_event_t(EVENT_NETWORK_STATUS) and sends to
//       g_app_event_queue (consumed by state_machine_task, which is the
//       ONLY writer to room_state.wifi_connected).
//
// Reconnect scheduling:
//   esp_wifi_connect() is called from the network_task context (NOT from the
//   Wi-Fi event handler), triggered by a one-shot FreeRTOS xTimer. This
//   avoids blocking the event-loop task and gives us precise control over
//   backoff timing. The timer is created in network_init and reused for the
//   lifetime of the system.
//
// SECURITY:
//   - SSID and passphrase are NEVER logged, NEVER compiled in, NEVER stored
//     in our own config module. They live in ESP-IDF Wi-Fi NVS namespace only.
//   - esp_matter Wi-Fi provisioning pushes credentials through the BLE
//     commissioning channel and into ESP-IDF Wi-Fi storage directly.

#include "network.h"

#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "state_machine.h"   // g_app_event_queue, app_event_t, network_status_t

static const char *TAG = "network";

// Wi-Fi SSID max length per IEEE 802.11 is 32 bytes. ESP-IDF wifi_config_t
// .sta.ssid is 32 bytes (no NUL terminator in the field itself; the API
// treats it as a counted byte string, not NUL-terminated).
#define WIFI_SSID_MAX_LEN  32U
#define WIFI_PWD_MAX_LEN   64U

#define NETWORK_TASK_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)
#define NETWORK_QUEUE_SEND_MS    20U     // bounded wait per task-architecture.md §5.3

// Reconnect policy (commissioning-lifecycle.md §3.4):
//   - Generic disconnect (beacon loss, AP restart, transient RF): exponential
//     backoff 1→2→4→8→16→32→60 s, capped at 60 s. Counter resets to 0 on
//     successful IP acquisition.
//   - Auth failure (WIFI_REASON_AUTH_FAIL / 4WAY_HANDSHAKE_TIMEOUT /
//     HANDSHAKE_TIMEOUT): the saved passphrase is wrong; retrying with the
//     same credentials is pointless. Allow up to AUTH_FAIL_MAX_ATTEMPTS
//     attempts (in case of transient AP-side issues) then STOP and wait for
//     re-commissioning.
#define WIFI_RECONNECT_BACKOFF_BASE_MS  1000U
#define WIFI_RECONNECT_BACKOFF_MAX_MS   60000U
#define WIFI_AUTH_FAIL_MAX_ATTEMPTS     3U

// Single-slot "latest status" queue (depth 1 + xQueueOverwrite). This fixes
// the event-group race where CONNECTED and DISCONNECTED bits could both be
// set, causing the task to emit contradictory states. With xQueueOverwrite,
// only the most recent status survives; the task consumes exactly one status
// per iteration. Producers (event handlers) never block.
static QueueHandle_t s_status_queue = NULL;

// Reconnect state. Written ONLY from the Wi-Fi event handler context (which
// is single-threaded: ESP-IDF runs the default event loop on one task).
// Read by network_task when firing the reconnect timer.
static uint8_t            s_reconnect_attempts   = 0;
static uint8_t            s_auth_fail_attempts   = 0;
static bool               s_provisioned          = false;
static bool               s_connected            = false;
static bool               s_reconnect_stopped    = false;   // auth-fail stop
static TimerHandle_t      s_reconnect_timer      = NULL;

// Forward declarations
static void enqueue_network_status(network_status_t status, uint32_t timestamp_ms);
static void send_network_status(network_status_t status, uint32_t timestamp_ms);
static void schedule_reconnect(void);
static void reconnect_timer_callback(TimerHandle_t timer);

// Returns true if the disconnect reason indicates an authentication /
// handshake failure (wrong passphrase). For these reasons, retrying with
// the same credentials is pointless — we count them separately and stop
// after WIFI_AUTH_FAIL_MAX_ATTEMPTS to avoid hammering the AP.
static bool reason_is_auth_fail(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return true;
    default:
        return false;
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;

    if (base != WIFI_EVENT) {
        return;
    }

    switch (id) {
    case WIFI_EVENT_STA_START:
        // STA started — connect immediately. This path is hit on the first
        // esp_wifi_start() and after esp_wifi_disconnect() + esp_wifi_connect().
        // No backoff here: this is the first connection attempt for this
        // session, not a reconnect.
        ESP_LOGI(TAG, "STA started, attempting connect");
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disc = data;
        s_connected = false;
        enqueue_network_status(NETWORK_STATUS_DISCONNECTED,
                               xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (s_reconnect_stopped) {
            // Already gave up due to auth fail; do not schedule further
            // reconnects. User must re-commission.
            ESP_LOGW(TAG, "STA disconnected (reason=%u); reconnect stopped "
                     "(needs re-commissioning)", (unsigned)disc->reason);
            break;
        }

        if (reason_is_auth_fail(disc->reason)) {
            s_auth_fail_attempts++;
            ESP_LOGW(TAG, "STA auth fail (reason=%u, attempt=%u/%u)",
                     (unsigned)disc->reason,
                     (unsigned)s_auth_fail_attempts,
                     (unsigned)WIFI_AUTH_FAIL_MAX_ATTEMPTS);
            if (s_auth_fail_attempts >= WIFI_AUTH_FAIL_MAX_ATTEMPTS) {
                ESP_LOGE(TAG, "STA auth fail max attempts reached; STOPPING "
                         "reconnect. Device needs re-commissioning with new "
                         "credentials (commissioning-lifecycle.md §3.4).");
                s_reconnect_stopped = true;
                break;
            }
            // Fall through to schedule_reconnect() — give the AP a chance to
            // recover (some APs have transient auth issues during roam).
        } else {
            ESP_LOGW(TAG, "STA disconnected (reason=%u); scheduling reconnect",
                     (unsigned)disc->reason);
        }

        schedule_reconnect();
        break;
    }

    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    s_connected = true;
    // Reset ALL reconnect state — we are back to a clean slate.
    s_reconnect_attempts = 0;
    s_auth_fail_attempts = 0;
    s_reconnect_stopped  = false;
    // If a reconnect timer was pending, cancel it — we are connected now.
    if (s_reconnect_timer != NULL) {
        xTimerStop(s_reconnect_timer, 0);
    }
    enqueue_network_status(NETWORK_STATUS_CONNECTED,
                           xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "STA got IP, connected");
}

// Compute the next backoff delay and (re)arm the one-shot reconnect timer.
// Called from the Wi-Fi event handler context. The actual esp_wifi_connect()
// call happens in reconnect_timer_callback (network_task timer-service
// context), so the event handler never blocks on Wi-Fi internals.
static void schedule_reconnect(void)
{
    if (s_reconnect_timer == NULL) {
        ESP_LOGE(TAG, "schedule_reconnect: timer is NULL (network_init "
                 "incomplete?)");
        return;
    }
    s_reconnect_attempts++;
    // Exponential backoff: 1, 2, 4, 8, 16, 32, 60, 60, 60, ...
    uint32_t delay_ms = WIFI_RECONNECT_BACKOFF_BASE_MS
                        << (s_reconnect_attempts - 1);
    if (delay_ms > WIFI_RECONNECT_BACKOFF_MAX_MS) {
        delay_ms = WIFI_RECONNECT_BACKOFF_MAX_MS;
    }
    // xTimerChangePeriod also (re)starts the timer.
    if (xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(delay_ms), 0)
        != pdPASS) {
        ESP_LOGE(TAG, "xTimerChangePeriod failed (delay=%u ms)",
                 (unsigned)delay_ms);
    }
    ESP_LOGI(TAG, "reconnect scheduled in %u ms (attempt=%u)",
             (unsigned)delay_ms, (unsigned)s_reconnect_attempts);
}

static void reconnect_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    // Runs in the FreeRTOS timer-service task context. Safe to call
    // esp_wifi_connect() here — it is non-blocking and just queues a connect
    // request internally. If it fails, the subsequent STA_DISCONNECTED event
    // will trigger another schedule_reconnect().
    if (s_connected || s_reconnect_stopped) {
        return;   // raced with a connection / auth-fail-stop
    }
    ESP_LOGI(TAG, "reconnect timer fired, calling esp_wifi_connect()");
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s — will retry on next disconnect",
                 esp_err_to_name(ret));
    }
}

esp_err_t network_init(void)
{
    if (s_status_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Single-slot queue for "latest network status". xQueueOverwrite ensures
    // only the most recent status is kept; producers never block, consumers
    // always read the latest. This replaces the previous event-group design
    // which could accumulate contradictory CONNECTED+DISCONNECTED bits.
    s_status_queue = xQueueCreate(1, sizeof(network_status_t));
    if (s_status_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // One-shot reconnect timer. Period is changed dynamically by
    // schedule_reconnect(); the timer is never auto-reloaded.
    s_reconnect_timer = xTimerCreate("wifi_reconnect",
                                     pdMS_TO_TICKS(WIFI_RECONNECT_BACKOFF_MAX_MS),
                                     pdFALSE,   // one-shot
                                     NULL,
                                     reconnect_timer_callback);
    if (s_reconnect_timer == NULL) {
        vQueueDelete(s_status_queue);
        s_status_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    // --- ESP-IDF Wi-Fi init order (esp-idf v5.4.1 wifi.rst) ---
    // 1) esp_netif_init() — creates the TCP/IP stack. Tolerate INVALID_STATE
    //    in case esp_matter or another caller already initialised it.
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(ret));
        goto fail_rollback;
    }

    // 2) Default event loop — required by esp_wifi event delivery. Tolerate
    //    INVALID_STATE because esp_matter may create it first.
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(ret));
        goto fail_rollback;
    }

    // 3) STA netif — must exist before esp_wifi_start() so IP events can bind.
    //    esp_netif_create_default_wifi_sta() asserts internally on duplicate;
    //    guard with a check via esp_netif_get_handle_from_ifkey.
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    // 4) Wi-Fi driver init with default config.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(ret));
        goto fail_rollback;
    }

    // 5) Register event handlers for reconnect logic.
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &on_wifi_event, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register WIFI_EVENT handler: %s", esp_err_to_name(ret));
        goto fail_rollback;
    }
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &on_ip_event, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register IP_EVENT handler: %s", esp_err_to_name(ret));
        goto fail_rollback_handler;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // 6) If ESP-IDF Wi-Fi NVS already has credentials (saved from a previous
    //    commissioning), start Wi-Fi now and let the on_wifi_event handler
    //    drive the first connection attempt. If no credentials are saved,
    //    esp_wifi_start() still succeeds but STA will stay unconnected until
    //    network_apply_provisioned_credentials() is called from the BLE
    //    commissioning path.
    //
    //    We detect saved credentials by loading the STA config and checking
    //    if the SSID field has any non-zero byte. ESP-IDF stores SSID as a
    //    32-byte field (not NUL-terminated when length == 32), so we must
    //    scan the whole field, not just the first byte. esp_wifi_get_config()
    //    returns ESP_ERR_NVS if nothing is stored, which we treat as
    //    "not provisioned".
    wifi_config_t existing_cfg = {0};
    ret = esp_wifi_get_config(WIFI_IF_STA, &existing_cfg);
    bool has_saved_ssid = false;
    if (ret == ESP_OK) {
        for (size_t i = 0; i < sizeof(existing_cfg.sta.ssid); ++i) {
            if (existing_cfg.sta.ssid[i] != 0) {
                has_saved_ssid = true;
                break;
            }
        }
    }
    if (has_saved_ssid) {
        s_provisioned = true;
        ESP_LOGI(TAG, "saved Wi-Fi credentials found; starting STA");
    } else {
        ESP_LOGI(TAG, "no saved Wi-Fi credentials; waiting for commissioning");
    }
    // Either way, start the radio. STA_START event will fire and, if
    // provisioned, on_wifi_event will call esp_wifi_connect().
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "init ok (STA mode; credentials via esp_matter provisioning)");
    return ESP_OK;

fail_rollback_handler:
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event);
fail_rollback:
    xTimerDelete(s_reconnect_timer, 0);
    s_reconnect_timer = NULL;
    vQueueDelete(s_status_queue);
    s_status_queue = NULL;
    return ret;
}

esp_err_t network_apply_provisioned_credentials(const char *ssid,
                                                const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Validate lengths against ESP-IDF wifi_config_t field sizes. SSID is a
    // 32-byte counted string (full 32 bytes valid, no NUL needed in the
    // field); password is 64-byte (NUL-terminated). Reject over-length inputs
    // instead of silently truncating — a truncated SSID connects to the wrong
    // network and a truncated password never authenticates.
    size_t ssid_len = strlen(ssid);
    size_t pwd_len  = strlen(password);
    if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN) {
        ESP_LOGE(TAG, "invalid SSID length %u (must be 1..%u)",
                 (unsigned)ssid_len, WIFI_SSID_MAX_LEN);
        return ESP_ERR_INVALID_ARG;
    }
    if (pwd_len > WIFI_PWD_MAX_LEN) {
        ESP_LOGE(TAG, "invalid password length %u (must be 0..%u)",
                 (unsigned)pwd_len, WIFI_PWD_MAX_LEN);
        return ESP_ERR_INVALID_ARG;
    }
    // Never log the SSID/password. Length-only log is acceptable for debugging.
    ESP_LOGI(TAG, "applying provisioned credentials (ssid_len=%u, pwd_len=%u)",
             (unsigned)ssid_len, (unsigned)pwd_len);

    wifi_config_t wifi_cfg = {0};
    // Copy exactly ssid_len bytes. .sta.ssid is a 32-byte field; in ESP-IDF
    // v5.4.1 wifi_sta_config_t has NO ssid_length member (only wifi_ap_config_t
    // has ssid_len). When ssid_len < 32 we NUL-terminate so the SSID also works
    // as a string; when ssid_len == 32 the field is full and esp_wifi treats it
    // as a counted 32-byte SSID (no NUL needed).
    memcpy(wifi_cfg.sta.ssid, ssid, ssid_len);
    if (ssid_len < WIFI_SSID_MAX_LEN) {
        wifi_cfg.sta.ssid[ssid_len] = '\0';
    }
    // Password is NUL-terminated in wifi_config_t; pwd_len < 64 leaves the
    // trailing NUL from {0} initialiser in place.
    memcpy(wifi_cfg.sta.password, password, pwd_len);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    s_provisioned       = true;
    // New credentials → reset reconnect / auth-fail state so a fresh attempt
    // is made even if the previous ones gave up.
    s_reconnect_attempts  = 0;
    s_auth_fail_attempts  = 0;
    s_reconnect_stopped   = false;

    // If esp_wifi_start() has not been called yet (network_init path with no
    // saved credentials), start it now. If already started, this is a no-op
    // for the radio; we still need to issue a connect for the new config.
    esp_wifi_start();
    esp_wifi_disconnect();   // ensure clean state before connecting
    esp_wifi_connect();

    // Notify state machine so it can reflect "provisioned, connecting" state.
    send_network_status(NETWORK_STATUS_PROVISIONED,
                        xTaskGetTickCount() * portTICK_PERIOD_MS);
    return ESP_OK;
}

bool network_is_connected(void)
{
    return s_connected;
}

// Overwrite the single-slot "latest status" queue. Called from event-loop
// task context (Wi-Fi/IP event handlers). Never blocks.
static void enqueue_network_status(network_status_t status, uint32_t timestamp_ms)
{
    // xQueueOverwrite always succeeds for a depth-1 queue: it discards any
    // pending item and writes the new one. This guarantees the consumer
    // (network_task) always sees the most recent status.
    (void)xQueueOverwrite(s_status_queue, &status);
    (void)timestamp_ms;   // timestamp is taken by the consumer at read time
}

// Build an app_event_t(EVENT_NETWORK_STATUS) and forward to the unified
// app_event_queue. Bounded 20 ms wait per task-architecture.md §5.3.
static void send_network_status(network_status_t status, uint32_t timestamp_ms)
{
    app_event_t ev = {
        .type         = EVENT_NETWORK_STATUS,
        .data.network = status,
        .timestamp_ms = timestamp_ms,
    };
    if (xQueueSend(g_app_event_queue, &ev,
                   pdMS_TO_TICKS(NETWORK_QUEUE_SEND_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "app_event_queue full; NETWORK_STATUS(%d) dropped",
                 (int)status);
        // TODO: increment a per-category drop counter and expose via
        //       diagnostics (task-architecture.md §5.3).
    }
}

void network_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    for (;;) {
        // Block on the single-slot latest-status queue. 2 s timeout feeds TWDT.
        // Each iteration consumes at most ONE status — no risk of emitting
        // contradictory CONNECTED+DISCONNECTED in the same wake-up.
        network_status_t status;
        if (xQueueReceive(s_status_queue, &status,
                          pdMS_TO_TICKS(NETWORK_TASK_TIMEOUT_MS)) == pdTRUE) {
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            send_network_status(status, now_ms);
        }
        // Timeout with no status → no state change to report; the state
        // machine preserves the last known network status.

        // Feed TWDT every iteration. Max gap = NETWORK_TASK_TIMEOUT_MS (2 s)
        // per task-architecture.md §7.2.
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}
