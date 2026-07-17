// PrivacySense Matter Room Hub - network.c
//
// Wi-Fi Station management. Skeleton implementation; the real provisioning
// + reconnect/backoff logic arrives in a follow-up commit.
//
// Event flow (task-architecture.md §3, §5.1, §6.1):
//   ESP-IDF Wi-Fi/IP event loop
//     → on_wifi_event / on_ip_event (event-loop task context)
//       → update s_connected / s_reconnect_attempts
//       → set bits in s_wifi_event_group
//   network_task
//     → blocks on s_wifi_event_group with 2 s timeout (TWDT feed)
//     → on wake, builds app_event_t(EVENT_NETWORK_STATUS) and sends to
//       g_app_event_queue (consumed by state_machine_task, which is the
//       ONLY writer to room_state.wifi_connected).
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

#include "state_machine.h"   // g_app_event_queue, app_event_t, network_status_t

static const char *TAG = "network";

// Wi-Fi SSID max length per IEEE 802.11 is 32 bytes. ESP-IDF wifi_config_t
// .sta.ssid is 32 bytes (no NUL terminator in the field itself; the API
// treats it as a counted byte string, not NUL-terminated).
#define WIFI_SSID_MAX_LEN  32U
#define WIFI_PWD_MAX_LEN   64U

#define NETWORK_TASK_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)
#define NETWORK_QUEUE_SEND_MS    20U     // bounded wait per task-architecture.md §5.3

// Single-slot "latest status" queue (depth 1 + xQueueOverwrite). This fixes
// the event-group race where CONNECTED and DISCONNECTED bits could both be
// set, causing the task to emit contradictory states. With xQueueOverwrite,
// only the most recent status survives; the task consumes exactly one status
// per iteration. Producers (event handlers) never block.
static QueueHandle_t s_status_queue = NULL;
static volatile bool      s_connected           = false;
static volatile bool      s_provisioned         = false;
static uint8_t            s_reconnect_attempts  = 0;

#define WIFI_RECONNECT_MAX_ATTEMPTS    5
#define WIFI_RECONNECT_BACKOFF_BASE_MS 1000U

// Forward declarations
static void enqueue_network_status(network_status_t status, uint32_t timestamp_ms);
static void send_network_status(network_status_t status, uint32_t timestamp_ms);

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base != WIFI_EVENT) {
        return;
    }

    switch (id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA started, attempting connect");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        s_connected = false;
        s_reconnect_attempts++;
        if (s_reconnect_attempts > WIFI_RECONNECT_MAX_ATTEMPTS) {
            ESP_LOGW(TAG, "STA disconnected, max retries (%d) hit; backing off",
                     WIFI_RECONNECT_MAX_ATTEMPTS);
            // TODO: exponential backoff timer (commissioning-lifecycle.md)
            s_reconnect_attempts = 0;
        } else {
            uint32_t delay = WIFI_RECONNECT_BACKOFF_BASE_MS *
                             (1U << (s_reconnect_attempts - 1));
            ESP_LOGW(TAG, "STA disconnected, reconnect #%u in %u ms",
                     s_reconnect_attempts, (unsigned)delay);
            // TODO: schedule delayed reconnect via timer (not vTaskDelay here,
            //       because we are in an event handler context).
        }
        // Latest-status queue: overwrite any pending status with DISCONNECTED.
        enqueue_network_status(NETWORK_STATUS_DISCONNECTED,
                               xTaskGetTickCount() * portTICK_PERIOD_MS);
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
    s_reconnect_attempts = 0;
    // Latest-status queue: overwrite any pending status with CONNECTED.
    enqueue_network_status(NETWORK_STATUS_CONNECTED,
                           xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "STA got IP, connected");
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

    // --- ESP-IDF Wi-Fi init order (esp-idf v5.4.1 wifi.rst) ---
    // 1) esp_netif_init() — creates the TCP/IP stack. Tolerate INVALID_STATE
    //    in case esp_matter or another caller already initialised it.
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2) Default event loop — required by esp_wifi event delivery. Tolerate
    //    INVALID_STATE because esp_matter may create it first.
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(ret));
        return ret;
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
        return ret;
    }

    // 5) Register event handlers for reconnect logic.
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &on_ip_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(TAG, "init ok (STA mode; credentials via esp_matter provisioning)");
    return ESP_OK;
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
    s_provisioned = true;
    ESP_ERROR_CHECK(esp_wifi_start());

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
             (unsigned)uxTaskGetStackHighWaterMark(NULL), uxTaskPriorityGet(NULL));

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
