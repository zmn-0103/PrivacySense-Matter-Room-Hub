// PrivacySense Matter Room Hub - network.c
//
// Wi-Fi Station observer (Phase 3 Step 2, post-Reviewer refactor).
//
// ESP-Matter's ESPWiFiDriver is the SOLE owner of Wi-Fi connection management
// (esp_wifi_set_config / connect / disconnect). This module:
//   - Initialises the Wi-Fi stack once at boot (esp_wifi_init / set_mode / start)
//     so ESPWiFiDriver inherits an already-started STA.
//   - Subscribes to WIFI_EVENT / IP_EVENT for state observation.
//   - Publishes NETWORK_STATUS to g_app_event_queue for state_machine_task.
//   - Manages SNTP time sync.
//   - Does NOT call esp_wifi_set_config / connect / disconnect.
//
// The local reconnect SM (network_reconnect_sm.c) tracks link state via events,
// but its WIFI_CONNECT / START_TIMER actions are ignored by execute_action —
// ESP-Matter + ESP-IDF auto-connect handle all connect / reconnect / reconfig.
//
// Callbacks enqueue commands non-blocking; network_task serialises all SM ops.

#include "network.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "network_reconnect_sm.h"
#include "state_machine.h"

static const char *TAG = "network";

#define NETWORK_TASK_TIMEOUT_MS  2000U
#define NETWORK_QUEUE_SEND_MS    20U

// ─── SNTP ───
static atomic_bool s_timezone_configured = false;
static atomic_bool s_time_synced = false;

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    if (!atomic_load(&s_time_synced)) {
        atomic_store(&s_time_synced, true);
        ESP_LOGI(TAG, "SNTP: time synced");
    }
}

static void init_sntp(void)
{
    bool tz_ok = (setenv("TZ", "CST-8", 1) == 0);
    if (!tz_ok) ESP_LOGW(TAG, "setenv(TZ) failed");
    tzset();
    if (tz_ok) atomic_store(&s_timezone_configured, true);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP: polling pool.ntp.org (%s)", tz_ok ? "CST-8" : "TZ unset");
}

bool network_time_is_synced(void)
{
    return atomic_load(&s_timezone_configured) && atomic_load(&s_time_synced);
}

// ─── Single-owner state ───
static net_sm_t s_sm;
static bool     s_network_initialized = false;

static atomic_bool   s_connected_atomic   = false;
static atomic_bool   s_provisioned_atomic = false;

// ─── Command queue payload ───
// Only link-event commands remain. WRITE_CREDENTIALS / TIMER_FIRED /
// RECONFIG_TIMEOUT were removed when network.c became a Wi-Fi observer
// (ESP-Matter owns connect / disconnect / credential injection).
typedef enum {
    NET_CMD_WIFI_STA_START = 0,
    NET_CMD_WIFI_DISCONNECTED,
    NET_CMD_IP_GOT_IP,
} net_cmd_type_t;

typedef struct {
    net_cmd_type_t type;
    uint8_t        disconnect_reason;
    uint32_t       sequence;           // monotonic, for ordering
} net_cmd_t;

// ─── Single bounded command transport ───
// All producers enter the same short critical section. This makes sequence
// assignment and insertion one operation, so callbacks cannot create the
// main-queue/overflow reordering that the previous two-container design had.
//
// If the ring saturates, producers enter spill mode. Existing ring entries
// are drained first; while spill mode is active no producer can bypass the
// spill slot. Link events coalesce to the latest observed link state.
// This is bounded, non-blocking, and records every overload episode.
#define NETWORK_CMD_RING_DEPTH  32U
typedef struct {
    net_cmd_t entries[NETWORK_CMD_RING_DEPTH];
    uint8_t head;
    uint8_t count;
    uint32_t next_sequence;
    uint32_t overrun_count;
    bool spill_active;
    bool spill_link_valid;
    net_cmd_t spill_link;
    TaskHandle_t task_handle;
} net_cmd_transport_t;

static net_cmd_transport_t s_cmd_transport;
static portMUX_TYPE s_cmd_transport_lock = portMUX_INITIALIZER_UNLOCKED;

// ─── NETWORK_STATUS pending (monotonic sequence) ───
static atomic_int    s_pending_status_val = -1;
static atomic_uint   s_pending_status_seq = 0;
static atomic_uint   s_delivered_status_seq = 0;

#ifdef CONFIG_NETWORK_DIAG_CONSOLE
// ─── Diagnostic snapshot (network_task-owned, mutex-protected) ───
static SemaphoreHandle_t  s_diag_mutex = NULL;
static network_diag_info_t s_diag_snapshot;

static void diag_publish_snapshot(void)
{
    if (!s_diag_mutex) return;
    if (xSemaphoreTake(s_diag_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    s_diag_snapshot.state                    = (int)s_sm.state;
    s_diag_snapshot.reconnect_attempts       = s_sm.reconnect_attempts;
    s_diag_snapshot.auth_fail_attempts       = s_sm.auth_fail_attempts;
    s_diag_snapshot.provisioned              = s_sm.provisioned;
    s_diag_snapshot.timer_armed              = s_sm.timer_armed;
    // Read overrun_count inside the transport lock.
    portENTER_CRITICAL(&s_cmd_transport_lock);
    s_diag_snapshot.ingress_overruns = s_cmd_transport.overrun_count;
    s_cmd_transport.overrun_count = 0;
    portEXIT_CRITICAL(&s_cmd_transport_lock);
    xSemaphoreGive(s_diag_mutex);
}

void network_get_diag_info(network_diag_info_t *info)
{
    if (!info || !s_diag_mutex) return;
    if (xSemaphoreTake(s_diag_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    *info = s_diag_snapshot;
    xSemaphoreGive(s_diag_mutex);
}

// Forward decl for queue storm.
static bool command_enqueue(net_cmd_t cmd);

void network_inject_queue_storm(void)
{
    uint32_t pre;
    portENTER_CRITICAL(&s_cmd_transport_lock);
    pre = s_cmd_transport.overrun_count;
    portEXIT_CRITICAL(&s_cmd_transport_lock);

    for (int i = 0; i < 40; i++) {
        net_cmd_t cmd = {
            .type = NET_CMD_WIFI_DISCONNECTED,
            .disconnect_reason = 201,
        };
        if (!command_enqueue(cmd)) break;
    }

    uint32_t post;
    portENTER_CRITICAL(&s_cmd_transport_lock);
    post = s_cmd_transport.overrun_count;
    portEXIT_CRITICAL(&s_cmd_transport_lock);

    ESP_LOGW(TAG, "queue storm: injected 40 cmds, overrun_count %" PRIu32 " -> %" PRIu32,
             pre, post);
}
#endif // CONFIG_NETWORK_DIAG_CONSOLE

static network_status_t status_to_public(net_sm_status_t s)
{
    switch (s) {
    case NET_SM_STATUS_CONNECTED:    return NETWORK_STATUS_CONNECTED;
    case NET_SM_STATUS_PROVISIONED:  return NETWORK_STATUS_PROVISIONED;
    default:                         return NETWORK_STATUS_DISCONNECTED;
    }
}

static bool command_is_link_event(net_cmd_type_t type)
{
    return type == NET_CMD_WIFI_STA_START ||
           type == NET_CMD_WIFI_DISCONNECTED ||
           type == NET_CMD_IP_GOT_IP;
}

static bool sequence_before(uint32_t lhs, uint32_t rhs)
{
    return (int32_t)(lhs - rhs) < 0;
}

// Non-blocking MPSC ingress. The critical section contains only bounded
// memory operations; task notification happens after the lock is released.
static bool command_enqueue(net_cmd_t cmd)
{
    TaskHandle_t task_to_notify;

    portENTER_CRITICAL(&s_cmd_transport_lock);
    cmd.sequence = ++s_cmd_transport.next_sequence;

    if (!s_cmd_transport.spill_active &&
        s_cmd_transport.count < NETWORK_CMD_RING_DEPTH) {
        uint8_t tail = (uint8_t)((s_cmd_transport.head +
                                 s_cmd_transport.count) %
                                NETWORK_CMD_RING_DEPTH);
        s_cmd_transport.entries[tail] = cmd;
        s_cmd_transport.count++;
    } else {
        s_cmd_transport.spill_active = true;
        s_cmd_transport.overrun_count++;

        // All commands are link events now — last-wins here represents the
        // latest observed physical state.
        s_cmd_transport.spill_link = cmd;
        s_cmd_transport.spill_link_valid = true;
    }

    task_to_notify = s_cmd_transport.task_handle;
    portEXIT_CRITICAL(&s_cmd_transport_lock);

    if (task_to_notify != NULL) xTaskNotifyGive(task_to_notify);
    return true;
}

// FIFO entries always predate spill entries. Once spill mode starts, all
// producers stay in spill mode until the ring and spill slot are empty,
// preventing newer commands from bypassing older retained commands.
static bool command_pop(net_cmd_t *out)
{
    bool have_command = false;

    portENTER_CRITICAL(&s_cmd_transport_lock);
    if (s_cmd_transport.count > 0) {
        *out = s_cmd_transport.entries[s_cmd_transport.head];
        s_cmd_transport.head = (uint8_t)((s_cmd_transport.head + 1U) %
                                         NETWORK_CMD_RING_DEPTH);
        s_cmd_transport.count--;
        have_command = true;
    } else if (s_cmd_transport.spill_link_valid) {
        *out = s_cmd_transport.spill_link;
        s_cmd_transport.spill_link_valid = false;
        have_command = true;
    }

    if (s_cmd_transport.count == 0 &&
        !s_cmd_transport.spill_link_valid) {
        s_cmd_transport.spill_active = false;
    }
    portEXIT_CRITICAL(&s_cmd_transport_lock);
    return have_command;
}

static uint32_t command_take_overrun_count(void)
{
    uint32_t count;
    portENTER_CRITICAL(&s_cmd_transport_lock);
    count = s_cmd_transport.overrun_count;
    s_cmd_transport.overrun_count = 0;
    portEXIT_CRITICAL(&s_cmd_transport_lock);
    return count;
}

static void command_transport_set_task(TaskHandle_t task)
{
    portENTER_CRITICAL(&s_cmd_transport_lock);
    s_cmd_transport.task_handle = task;
    portEXIT_CRITICAL(&s_cmd_transport_lock);
}

// ─── Event handlers (non-blocking) ───
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) return;
    switch (id) {
    case WIFI_EVENT_STA_START: {
        ESP_LOGI(TAG, "STA started");
        net_cmd_t cmd = { .type = NET_CMD_WIFI_STA_START };
        if (!command_enqueue(cmd)) ESP_LOGE(TAG, "STA_START enqueue failed");
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disc = data;
        ESP_LOGW(TAG, "STA disconnected (reason=%u)", (unsigned)disc->reason);
        net_cmd_t cmd = { .type = NET_CMD_WIFI_DISCONNECTED,
                          .disconnect_reason = disc->reason };
        if (!command_enqueue(cmd)) ESP_LOGE(TAG, "DISCONNECTED enqueue failed");
        break;
    }
    default: break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "STA got IP");
        net_cmd_t cmd = { .type = NET_CMD_IP_GOT_IP };
        if (!command_enqueue(cmd)) ESP_LOGE(TAG, "GOT_IP enqueue failed");
    }
}

// ─── Action executor (observer mode) ───
// Only NOTIFY_STATUS is honoured. WIFI_CONNECT / WIFI_DISCONNECT / START_TIMER /
// STOP_TIMER are silently ignored — ESP-Matter owns Wi-Fi connection management.
// The SM still tracks these actions internally (e.g. timer_armed), but network.c
// does not act on them.
static void execute_action(const net_sm_action_t *act)
{
    if (!(act->flags & NET_SM_ACT_NOTIFY_STATUS)) return;

    network_status_t pub = status_to_public(act->notify_status);
    atomic_store(&s_connected_atomic, pub == NETWORK_STATUS_CONNECTED);
    uint32_t seq = atomic_fetch_add(&s_pending_status_seq, 1) + 1;
    app_event_t ev = {
        .type = EVENT_NETWORK_STATUS, .data.network = pub,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
    if (xQueueSend(g_app_event_queue, &ev,
                   pdMS_TO_TICKS(NETWORK_QUEUE_SEND_MS)) == pdTRUE) {
        atomic_store(&s_delivered_status_seq, seq);
    } else {
        atomic_store(&s_pending_status_val, (int)pub);
        atomic_store(&s_pending_status_seq, seq);
        ESP_LOGW(TAG, "app_event_queue full; STATUS(%d) stashed seq=%" PRIu32,
                 (int)pub, seq);
    }
    if (act->notify_status == NET_SM_STATUS_STOPPED) {
        ESP_LOGE(TAG, "reconnect STOPPED");
    }
}

// ─── Command processor ───
static void process_command(const net_cmd_t *cmd)
{
    net_sm_event_t ev = { 0 };
    switch (cmd->type) {
    case NET_CMD_WIFI_STA_START:
        ev.type = NET_SM_EVENT_STA_START;
        ev.has_saved_creds = atomic_load(&s_provisioned_atomic);
        break;
    case NET_CMD_WIFI_DISCONNECTED:
        ev.type = NET_SM_EVENT_DISCONNECTED;
        ev.disconnect_reason = cmd->disconnect_reason;
        break;
    case NET_CMD_IP_GOT_IP:
        ev.type = NET_SM_EVENT_GOT_IP;
        break;
    default:
        return;
    }

    net_sm_action_t act;
    bool changed = net_sm_step(&s_sm, &ev, &act);
    if (act.flags != 0) execute_action(&act);
    if (changed) {
        ESP_LOGI(TAG, "sm: event=%d -> state=%d attempts=%u auth_fail=%u",
                 (int)ev.type, (int)s_sm.state,
                 (unsigned)s_sm.reconnect_attempts,
                 (unsigned)s_sm.auth_fail_attempts);
    }
}

// ─── Reconcile pending events ───
static void reconcile_pending_events(void)
{
    // Retry stashed NETWORK_STATUS; only deliver if sequence is newer.
    int stashed = atomic_load(&s_pending_status_val);
    uint32_t stashed_seq = atomic_load(&s_pending_status_seq);
    uint32_t delivered_seq = atomic_load(&s_delivered_status_seq);
    if (stashed >= 0 && sequence_before(delivered_seq, stashed_seq)) {
        network_status_t pub = (network_status_t)stashed;
        app_event_t ev = {
            .type = EVENT_NETWORK_STATUS, .data.network = pub,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        if (xQueueSend(g_app_event_queue, &ev,
                       pdMS_TO_TICKS(NETWORK_QUEUE_SEND_MS)) == pdTRUE) {
            atomic_store(&s_delivered_status_seq, stashed_seq);
            atomic_store(&s_pending_status_val, -1);
        }
    }
}

esp_err_t network_init(void)
{
    if (s_network_initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = ESP_ERR_NO_MEM;
    memset(&s_cmd_transport, 0, sizeof(s_cmd_transport));

    #ifdef CONFIG_NETWORK_DIAG_CONSOLE
    s_diag_mutex = xSemaphoreCreateMutex();
    if (!s_diag_mutex) goto fail_rollback;
    #endif

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) goto fail_rollback;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) goto fail_rollback;

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        if (esp_netif_create_default_wifi_sta() == NULL) {
            ESP_LOGE(TAG, "failed to create default WiFi STA netif");
            ret = ESP_FAIL;
            goto fail_rollback;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) goto fail_rollback;

    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL);
    if (ret != ESP_OK) goto fail_rollback;
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL);
    if (ret != ESP_OK) goto fail_rollback_wifi_handler;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) goto fail_rollback_both_handlers;

    wifi_config_t existing_cfg = {0};
    ret = esp_wifi_get_config(WIFI_IF_STA, &existing_cfg);
    bool has_saved_ssid = false;
    if (ret == ESP_OK) {
        for (size_t i = 0; i < sizeof(existing_cfg.sta.ssid); ++i) {
            if (existing_cfg.sta.ssid[i] != 0) { has_saved_ssid = true; break; }
        }
    }
    atomic_store(&s_provisioned_atomic, has_saved_ssid);
    net_sm_init(&s_sm, has_saved_ssid);

    if (has_saved_ssid) ESP_LOGI(TAG, "saved credentials found");
    else ESP_LOGI(TAG, "no saved credentials");

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(ret));
        goto fail_rollback_both_handlers;
    }

    init_sntp();
    s_network_initialized = true;
    ESP_LOGI(TAG, "init ok (observer mode — ESP-Matter owns Wi-Fi connect)");
    return ESP_OK;

fail_rollback_both_handlers:
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event);
fail_rollback_wifi_handler:
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event);
fail_rollback:
    #ifdef CONFIG_NETWORK_DIAG_CONSOLE
    if (s_diag_mutex) { vSemaphoreDelete(s_diag_mutex); s_diag_mutex = NULL; }
    #endif
    memset(&s_cmd_transport, 0, sizeof(s_cmd_transport));
    return ret;
}

// ─── Commissioning callback (observer mode stub) ───
// ESP-Matter's ESPWiFiDriver owns Wi-Fi credential injection. This function
// is kept for ABI compatibility with network_diag.c but always returns
// ESP_ERR_NOT_SUPPORTED. Use a Matter controller (chip-tool / mobile app) to
// commission the device and inject Wi-Fi credentials via the Network
// Commissioning cluster.
esp_err_t network_apply_provisioned_credentials(const char *ssid,
                                                const char *password)
{
    (void)ssid;
    (void)password;
    ESP_LOGW(TAG, "network_apply_provisioned_credentials: no-op (ESP-Matter owns "
             "Wi-Fi cred injection). Use a Matter controller to commission.");
    return ESP_ERR_NOT_SUPPORTED;
}

bool network_is_connected(void)
{
    return atomic_load(&s_connected_atomic);
}

bool network_cred_write_permanent_failure(void)
{
    // No credential management in observer mode — always false.
    return false;
}

void network_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    command_transport_set_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    uint32_t loop = 0;
    uint32_t last_diag_tick = xTaskGetTickCount();

    for (;;) {
        loop++;
        bool did_work = false;

        // Drain the single ordered ingress. The bounded budget prevents a
        // noisy producer from starving diagnostics or the watchdog.
        for (uint32_t i = 0; i < NETWORK_CMD_RING_DEPTH + 2U; ++i) {
            net_cmd_t cmd;
            if (!command_pop(&cmd)) break;
            process_command(&cmd);
            did_work = true;
            ESP_ERROR_CHECK(esp_task_wdt_reset());
        }

        reconcile_pending_events();
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        #ifdef CONFIG_NETWORK_DIAG_CONSOLE
        diag_publish_snapshot();
        #endif

        // Periodic diagnostics include transport saturation and stack margin.
        uint32_t now_tick = xTaskGetTickCount();
        if ((now_tick - last_diag_tick) >= pdMS_TO_TICKS(30000)) {
            last_diag_tick = now_tick;
            uint32_t overruns = command_take_overrun_count();
            if (overruns > 0) {
                ESP_LOGW(TAG, "diag: loop=%u stack_hwm=%u ingress_overruns=%" PRIu32
                         " st=%d", (unsigned)loop,
                         (unsigned)uxTaskGetStackHighWaterMark(NULL), overruns,
                         (int)s_sm.state);
            } else {
                ESP_LOGI(TAG, "diag: loop=%u stack_hwm=%u ingress_overruns=0 st=%d",
                         (unsigned)loop,
                         (unsigned)uxTaskGetStackHighWaterMark(NULL),
                         (int)s_sm.state);
            }
        }

        if (did_work) continue;

        TickType_t wait_ticks = pdMS_TO_TICKS(NETWORK_TASK_TIMEOUT_MS);
        if (wait_ticks == 0) wait_ticks = 1;
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
    }
}
