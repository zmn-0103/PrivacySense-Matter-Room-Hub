// PrivacySense Matter Room Hub - network.c
//
// Wi-Fi Station management with single-owner state (phase 3).
// Callbacks enqueue commands non-blocking; network_task serialises all SM.

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

#define WIFI_SSID_MAX_LEN  32U
#define WIFI_PWD_MAX_LEN   64U
#define NETWORK_TASK_TIMEOUT_MS  2000U
#define NETWORK_QUEUE_SEND_MS    20U
#define MAX_ACTION_RECOVERY_ITER  3U
#define MAX_CRED_WRITE_RETRIES    3U
#define CRED_WRITE_BACKOFF_MS  1000U

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
typedef enum {
    NET_CMD_WIFI_STA_START = 0,
    NET_CMD_WIFI_DISCONNECTED,
    NET_CMD_IP_GOT_IP,
    NET_CMD_TIMER_FIRED,
    NET_CMD_WRITE_CREDENTIALS,
    NET_CMD_RECONFIG_TIMEOUT,
} net_cmd_type_t;

typedef struct {
    net_cmd_type_t type;
    uint8_t        disconnect_reason;
    uint8_t        timer_generation;   // valid for TIMER_FIRED
    uint32_t       sequence;           // monotonic, for ordering
} net_cmd_t;

// ─── Single bounded command transport ───
// All producers enter the same short critical section. This makes sequence
// assignment and insertion one operation, so callbacks cannot create the
// main-queue/overflow reordering that the previous two-container design had.
//
// If the ring saturates, producers enter spill mode. Existing ring entries
// are drained first; while spill mode is active no producer can bypass the
// spill slots. Link events coalesce to the latest observed link state, while
// the one outstanding credential transaction retains its first command.
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
    bool spill_credential_valid;
    net_cmd_t spill_credential;
    TaskHandle_t task_handle;
} net_cmd_transport_t;

static net_cmd_transport_t s_cmd_transport;
static portMUX_TYPE s_cmd_transport_lock = portMUX_INITIALIZER_UNLOCKED;

// ─── Credential mailbox ───
// network_task holds s_cred_mutex for copy + NVS write + cleanup (sole writer).
// commissioning callback uses take(0); if busy, returns error.
static SemaphoreHandle_t s_cred_mutex = NULL;
static wifi_config_t     s_pending_wifi_cfg;
static bool              s_pending_creds_valid = false;

typedef enum {
    CRED_WRITE_OK,
    CRED_WRITE_TRANSIENT,    // mutex busy — retry, don't count
    CRED_WRITE_RETRYABLE,    // NVS write fail — count toward MAX_CRED_WRITE_RETRIES
    CRED_WRITE_FATAL,
} cred_write_result_t;

// ─── NETWORK_STATUS pending (monotonic sequence) ───
static atomic_int    s_pending_status_val = -1;
static atomic_uint   s_pending_status_seq = 0;
static atomic_uint   s_delivered_status_seq = 0;

// ─── NVS write retry state (written only by network_task) ───
static uint8_t  s_cred_write_retries = 0;
static bool     s_cred_write_retry_pending = false;
static uint32_t s_cred_write_next_retry_ms = 0;
static atomic_bool s_cred_write_permanent_failure = false;
static bool     s_cred_cleanup_pending = false;

// ─── Wi-Fi start retry (independent from NVS write) ───
#define MAX_WIFI_START_RETRIES  5U
#define WIFI_START_BACKOFF_MS  2000U
static uint8_t  s_wifi_start_retries = 0;
static bool     s_wifi_start_retry_pending = false;
static uint32_t s_wifi_start_next_retry_ms = 0;

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
    s_diag_snapshot.cred_write_retry_pending = s_cred_write_retry_pending;
    s_diag_snapshot.wifi_start_retry_pending = s_wifi_start_retry_pending;
    s_diag_snapshot.reconnect_deadline_valid = s_reconnect_deadline_valid;
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

// ─── Fault injection ───
static atomic_int s_fault_flags;

void network_inject_fault(net_fault_type_t fault)
{
    atomic_fetch_or(&s_fault_flags, (int)(1u << (unsigned)fault));
    ESP_LOGW(TAG, "fault injected: %d", (int)fault);
}

void network_clear_fault(net_fault_type_t fault)
{
    atomic_fetch_and(&s_fault_flags, (int)~(1u << (unsigned)fault));
    ESP_LOGI(TAG, "fault cleared: %d", (int)fault);
}

void network_clear_all_faults(void)
{
    atomic_store(&s_fault_flags, 0);
    ESP_LOGI(TAG, "all faults cleared");
}

static bool fault_active(net_fault_type_t fault)
{
    return (atomic_load(&s_fault_flags) & (int)(1u << (unsigned)fault)) != 0;
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

// ─── Task-owned reconnect deadline ───
// No FreeRTOS software timer is used. network_task owns both fields and
// injects TIMER_FIRED directly, eliminating callback-generation races.
static bool     s_reconnect_deadline_valid = false;
static uint32_t s_reconnect_deadline_ms = 0;
static bool     s_reconfig_deadline_valid = false;
static uint32_t s_reconfig_started_ms = 0;
static uint32_t s_reconfig_deadline_ms = 0;

static void execute_action(const net_sm_action_t *act);
static void process_command(const net_cmd_t *cmd);

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
    bool retained = true;

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

        if (command_is_link_event(cmd.type)) {
            // ESP Wi-Fi/IP callbacks run on the default event-loop task, so
            // last-wins here represents the latest observed physical state.
            s_cmd_transport.spill_link = cmd;
            s_cmd_transport.spill_link_valid = true;
        } else if (cmd.type == NET_CMD_WRITE_CREDENTIALS) {
            // The credential mailbox admits only one transaction. Preserve
            // the first command/sequence; retries are task-owned deadlines.
            if (!s_cmd_transport.spill_credential_valid) {
                s_cmd_transport.spill_credential = cmd;
                s_cmd_transport.spill_credential_valid = true;
            }
        } else {
            // TIMER_FIRED and RECONFIG_TIMEOUT are generated directly by
            // network_task and must never enter this producer path.
            retained = false;
        }
    }

    task_to_notify = s_cmd_transport.task_handle;
    portEXIT_CRITICAL(&s_cmd_transport_lock);

    if (task_to_notify != NULL) xTaskNotifyGive(task_to_notify);
    return retained;
}

// FIFO entries always predate spill entries. Once spill mode starts, all
// producers stay in spill mode until the ring and both spill slots are empty,
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
    } else if (s_cmd_transport.spill_link_valid ||
               s_cmd_transport.spill_credential_valid) {
        bool take_link = s_cmd_transport.spill_link_valid;
        if (take_link && s_cmd_transport.spill_credential_valid) {
            take_link = sequence_before(s_cmd_transport.spill_link.sequence,
                                        s_cmd_transport.spill_credential.sequence);
        }

        if (take_link) {
            *out = s_cmd_transport.spill_link;
            s_cmd_transport.spill_link_valid = false;
        } else {
            *out = s_cmd_transport.spill_credential;
            s_cmd_transport.spill_credential_valid = false;
        }
        have_command = true;
    }

    if (s_cmd_transport.count == 0 &&
        !s_cmd_transport.spill_link_valid &&
        !s_cmd_transport.spill_credential_valid) {
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

// ─── Action executor with recovery loop ───
static void execute_action(const net_sm_action_t *act)
{
    net_sm_action_t cur = *act;
    for (uint32_t iter = 0; iter < MAX_ACTION_RECOVERY_ITER; ++iter) {
        uint32_t f = cur.flags;

        if (f & NET_SM_ACT_STOP_TIMER) {
            s_reconnect_deadline_valid = false;
        }

        if (f & NET_SM_ACT_START_TIMER) {
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            s_reconnect_deadline_ms = now_ms + cur.timer_delay_ms;
            s_reconnect_deadline_valid = true;
            ESP_LOGI(TAG, "reconnect timer armed for %" PRIu32 " ms", cur.timer_delay_ms);
        }

        if (f & NET_SM_ACT_WIFI_DISCONNECT) {
            esp_err_t ret = esp_wifi_disconnect();
            if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
                ESP_LOGW(TAG, "esp_wifi_disconnect: %s", esp_err_to_name(ret));
                net_sm_event_t fe = { .type = NET_SM_EVENT_ACTION_FAILED,
                                      .fail_type = NET_SM_FAIL_WIFI_DISCONNECT };
                net_sm_action_t recovery;
                if (net_sm_step(&s_sm, &fe, &recovery)) {
                    if ((recovery.flags & NET_SM_ACT_NOTIFY_STATUS) == 0 &&
                        (cur.flags & NET_SM_ACT_NOTIFY_STATUS)) {
                        recovery.flags |= NET_SM_ACT_NOTIFY_STATUS;
                        recovery.notify_status = cur.notify_status;
                    }
                    cur = recovery;
                    continue;
                }
                if (cur.flags & NET_SM_ACT_NOTIFY_STATUS) goto do_notify;
                return;
            }
        }

        if (f & NET_SM_ACT_WIFI_CONNECT) {
            esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(ret));
                net_sm_event_t fe = { .type = NET_SM_EVENT_ACTION_FAILED,
                                      .fail_type = NET_SM_FAIL_WIFI_CONNECT };
                net_sm_action_t recovery;
                if (net_sm_step(&s_sm, &fe, &recovery)) {
                    if ((recovery.flags & NET_SM_ACT_NOTIFY_STATUS) == 0 &&
                        (cur.flags & NET_SM_ACT_NOTIFY_STATUS)) {
                        recovery.flags |= NET_SM_ACT_NOTIFY_STATUS;
                        recovery.notify_status = cur.notify_status;
                    }
                    cur = recovery;
                    continue;
                }
                if (cur.flags & NET_SM_ACT_NOTIFY_STATUS) goto do_notify;
                return;
            }
        }

do_notify:
        if (f & NET_SM_ACT_NOTIFY_STATUS) {
            network_status_t pub = status_to_public(cur.notify_status);
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
            if (cur.notify_status == NET_SM_STATUS_STOPPED) {
                ESP_LOGE(TAG, "reconnect STOPPED");
            }
        }
        return;
    }
    ESP_LOGE(TAG, "action recovery loop exhausted");
}

// ─── Mailbox cleanup helpers ───
// Returns true if mailbox was cleared under mutex, setting permanent_failure.
// Retry-safe: returns true even if mailbox was already cleared, so callers
// can unconditionally advance to final-failure state.
static bool clear_credential_mailbox(void)
{
    if (xSemaphoreTake(s_cred_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (s_pending_creds_valid) {
        s_pending_creds_valid = false;
        explicit_bzero(&s_pending_wifi_cfg, sizeof(s_pending_wifi_cfg));
    }
    atomic_store(&s_cred_write_permanent_failure, true);
    xSemaphoreGive(s_cred_mutex);
    return true;
}

static void cred_write_final_failure(void)
{
    ESP_LOGE(TAG, "cred write failed; giving up");
    s_cred_write_retry_pending = false;
    if (clear_credential_mailbox()) {
        s_cred_cleanup_pending = false;
        s_cred_write_retries = 0;
    } else {
        s_cred_cleanup_pending = true;
    }
}

// Returns true if a retry was scheduled; false if max retries exhausted.
// `count` true for actual NVS write failures, false for transient (mutex busy).
static bool handle_cred_write_retry(bool count)
{
    if (count) {
        s_cred_write_retries++;
        if (s_cred_write_retries >= MAX_CRED_WRITE_RETRIES) {
            cred_write_final_failure();
            return false;
        }
    }
    s_cred_write_next_retry_ms =
        xTaskGetTickCount() * portTICK_PERIOD_MS + CRED_WRITE_BACKOFF_MS;
    s_cred_write_retry_pending = true;
    return true;
}

// Wi-Fi start retry with independent limit (P1-5).
static bool handle_wifi_start_retry(void)
{
    s_wifi_start_retries++;
    if (s_wifi_start_retries >= MAX_WIFI_START_RETRIES) {
        ESP_LOGE(TAG, "esp_wifi_start failed %u times; giving up",
                 s_wifi_start_retries);
        cred_write_final_failure();
        s_wifi_start_retries = 0;
        return false;
    }
    s_wifi_start_next_retry_ms =
        xTaskGetTickCount() * portTICK_PERIOD_MS + WIFI_START_BACKOFF_MS;
    s_wifi_start_retry_pending = true;
    return true;
}

// ─── Write credentials to NVS ───
// Holds s_cred_mutex for copy + NVS write; clears mailbox only on success
// (or final failure via cred_write_final_failure).
// Callback uses take(0) so it is never blocked — it returns error if
// the mutex is held.
static cred_write_result_t write_pending_credentials(void)
{
    if (xSemaphoreTake(s_cred_mutex, 0) != pdTRUE) {
        return CRED_WRITE_TRANSIENT;  // mutex busy, don't count
    }
    if (!s_pending_creds_valid) {
        xSemaphoreGive(s_cred_mutex);
        return CRED_WRITE_FATAL;
    }

    // Copy credentials locally but do NOT destroy the mailbox yet —
    // if NVS write fails we need the original for retry.
    wifi_config_t local_cfg = s_pending_wifi_cfg;
    // Mutex still held — callback take(0) will fail, which is safe.

#ifdef CONFIG_NETWORK_DIAG_CONSOLE
    if (fault_active(NET_FAULT_NVS_WRITE_FAIL)) {
        network_clear_fault(NET_FAULT_NVS_WRITE_FAIL);
        ESP_LOGW(TAG, "fault: injecting NVS write failure");
        xSemaphoreGive(s_cred_mutex);
        return CRED_WRITE_RETRYABLE;
    }
#endif

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &local_cfg);
    explicit_bzero(&local_cfg, sizeof(local_cfg));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config: %s", esp_err_to_name(ret));
        // Mailbox still valid for retry; mutex released.
        xSemaphoreGive(s_cred_mutex);
        return CRED_WRITE_RETRYABLE;
    }

    // NVS write succeeded — now clear the mailbox.
    s_pending_creds_valid = false;
    explicit_bzero(&s_pending_wifi_cfg, sizeof(s_pending_wifi_cfg));
    atomic_store(&s_cred_write_permanent_failure, false);
    xSemaphoreGive(s_cred_mutex);

    ESP_LOGI(TAG, "credentials written to NVS");
    return CRED_WRITE_OK;
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
        #ifdef CONFIG_NETWORK_DIAG_CONSOLE
        if (fault_active(NET_FAULT_BLOCK_DISCONNECT_IN_RECONFIG) &&
            s_sm.state == NET_SM_STATE_RECONFIGURING) {
            ESP_LOGW(TAG, "fault: dropping DISCONNECTED in RECONFIGURING");
            return;
        }
        #endif
        ev.type = NET_SM_EVENT_DISCONNECTED;
        ev.disconnect_reason = cmd->disconnect_reason;
        break;
    case NET_CMD_IP_GOT_IP:
        ev.type = NET_SM_EVENT_GOT_IP;
        break;
    case NET_CMD_TIMER_FIRED:
        ev.type = NET_SM_EVENT_TIMER_FIRED;
        ev.timer_generation = cmd->timer_generation;
        break;
    case NET_CMD_WRITE_CREDENTIALS: {
        esp_err_t sr = esp_wifi_start();
        if (sr != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(sr));
            if (!handle_wifi_start_retry()) return;
            return;
        }
        s_wifi_start_retries = 0;
        s_wifi_start_retry_pending = false;

        cred_write_result_t wr = write_pending_credentials();
        switch (wr) {
        case CRED_WRITE_OK:
            s_cred_write_retries = 0;
            s_cred_write_retry_pending = false;
            atomic_store(&s_provisioned_atomic, true);
            ev.type = NET_SM_EVENT_PROVISIONED;
            break;
        case CRED_WRITE_TRANSIENT:
            if (!handle_cred_write_retry(false)) return;
            return;
        case CRED_WRITE_RETRYABLE:
            if (!handle_cred_write_retry(true)) return;
            return;
        case CRED_WRITE_FATAL:
            cred_write_final_failure();
            return;
        }
        break;
    }
    case NET_CMD_RECONFIG_TIMEOUT:
        ev.type = NET_SM_EVENT_RECONFIG_TIMEOUT;
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
    s_reconnect_deadline_valid = false;
    s_reconfig_deadline_valid = false;

    s_cred_mutex = xSemaphoreCreateMutex();
    if (!s_cred_mutex) goto fail_rollback;

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
    ESP_LOGI(TAG, "init ok");
    return ESP_OK;

fail_rollback_both_handlers:
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event);
fail_rollback_wifi_handler:
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event);
fail_rollback:
    if (s_cred_mutex) { vSemaphoreDelete(s_cred_mutex); s_cred_mutex = NULL; }
    memset(&s_cmd_transport, 0, sizeof(s_cmd_transport));
    s_reconnect_deadline_valid = false;
    s_reconfig_deadline_valid = false;
    return ret;
}

// ─── Commissioning callback ───
esp_err_t network_apply_provisioned_credentials(const char *ssid,
                                                const char *password)
{
    if (!s_network_initialized || s_cred_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    size_t ssid_len = strlen(ssid);
    size_t pwd_len  = strlen(password);
    if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN) return ESP_ERR_INVALID_ARG;
    if (pwd_len > WIFI_PWD_MAX_LEN) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "credential receipt (ssid_len=%u, pwd_len=%u)",
             (unsigned)ssid_len, (unsigned)pwd_len);

    // Non-blocking: take(0). If busy (network_task writing), return error.
    if (xSemaphoreTake(s_cred_mutex, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cred mailbox busy");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pending_creds_valid) {
        xSemaphoreGive(s_cred_mutex);
        ESP_LOGW(TAG, "previous credentials unconsumed");
        return ESP_ERR_INVALID_STATE;
    }

    // Write credentials into the mailbox (callback only owns s_cred_mutex;
    // retry state is owned by network_task and never touched here — P1-4).
    atomic_store(&s_cred_write_permanent_failure, false);  // only safe under mutex
    memset(&s_pending_wifi_cfg, 0, sizeof(s_pending_wifi_cfg));
    memcpy(s_pending_wifi_cfg.sta.ssid, ssid, ssid_len);
    if (ssid_len < WIFI_SSID_MAX_LEN) s_pending_wifi_cfg.sta.ssid[ssid_len] = '\0';
    memcpy(s_pending_wifi_cfg.sta.password, password, pwd_len);
    s_pending_creds_valid = true;
    xSemaphoreGive(s_cred_mutex);

    net_cmd_t cmd = { .type = NET_CMD_WRITE_CREDENTIALS };
    (void)command_enqueue(cmd);  // credential commands always have a spill slot
    return ESP_OK;
}

bool network_is_connected(void)
{
    return atomic_load(&s_connected_atomic);
}

bool network_cred_write_permanent_failure(void)
{
    return atomic_load(&s_cred_write_permanent_failure);
}

static bool deadline_due(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t deadline_remaining_ms(uint32_t now_ms, uint32_t deadline_ms)
{
    int32_t remaining = (int32_t)(deadline_ms - now_ms);
    return remaining > 0 ? (uint32_t)remaining : 0U;
}

static void reduce_wait_to_deadline(uint32_t now_ms, uint32_t deadline_ms,
                                    uint32_t *wait_ms)
{
    uint32_t remaining = deadline_remaining_ms(now_ms, deadline_ms);
    if (remaining < *wait_ms) *wait_ms = remaining;
}

static uint32_t network_next_wait_ms(uint32_t now_ms)
{
    uint32_t wait_ms = NETWORK_TASK_TIMEOUT_MS;

    if (s_reconnect_deadline_valid) {
        reduce_wait_to_deadline(now_ms, s_reconnect_deadline_ms, &wait_ms);
    }
    if (s_cred_write_retry_pending) {
        reduce_wait_to_deadline(now_ms, s_cred_write_next_retry_ms, &wait_ms);
    }
    if (s_wifi_start_retry_pending) {
        reduce_wait_to_deadline(now_ms, s_wifi_start_next_retry_ms, &wait_ms);
    }
    if (s_cred_cleanup_pending && wait_ms > 50U) wait_ms = 50U;

    if (s_reconfig_deadline_valid) {
        reduce_wait_to_deadline(now_ms, s_reconfig_deadline_ms, &wait_ms);
    }
    return wait_ms;
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

        // Drain the single ordered ingress before task-owned deadlines. The
        // bounded budget prevents a noisy producer from starving retries,
        // timeout checks, diagnostics, or the watchdog.
        for (uint32_t i = 0; i < NETWORK_CMD_RING_DEPTH + 2U; ++i) {
            net_cmd_t cmd;
            if (!command_pop(&cmd)) break;
            process_command(&cmd);
            did_work = true;
            ESP_ERROR_CHECK(esp_task_wdt_reset());
        }

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Credential write retry (NVS write count-limited).
        if (s_cred_write_retry_pending &&
            deadline_due(now_ms, s_cred_write_next_retry_ms)) {
            s_cred_write_retry_pending = false;
            net_cmd_t cmd = { .type = NET_CMD_WRITE_CREDENTIALS };
            process_command(&cmd);
            did_work = true;
        }

        // Wi-Fi start retry (independent count-limited).
        if (s_wifi_start_retry_pending &&
            deadline_due(now_ms, s_wifi_start_next_retry_ms)) {
            s_wifi_start_retry_pending = false;
            net_cmd_t cmd = { .type = NET_CMD_WRITE_CREDENTIALS };
            process_command(&cmd);
            did_work = true;
        }

        // Mailbox cleanup retry (deferred from cred_write_final_failure).
        if (s_cred_cleanup_pending) {
            if (clear_credential_mailbox()) {
                s_cred_cleanup_pending = false;
                s_cred_write_retries = 0;
            }
            did_work = true;
        }

        // Defensive support for the pure SM's ACTION_FAILED contract. The
        // deadline implementation itself cannot fail to arm.
        if (s_sm.state == NET_SM_STATE_DISCONNECTED && s_sm.timer_arm_pending) {
            s_sm.timer_arm_pending = false;
            net_sm_action_t act = { 0 };
            act.flags = NET_SM_ACT_START_TIMER;
            act.timer_delay_ms = net_sm_compute_backoff_ms(s_sm.reconnect_attempts);
            s_sm.timer_armed = true;
            execute_action(&act);
            did_work = true;
        }

        // Reconnect expiry is generated and consumed by the owning task. No
        // callback can retain or relabel an event from an older arm cycle.
        now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (s_reconnect_deadline_valid &&
            deadline_due(now_ms, s_reconnect_deadline_ms)) {
            s_reconnect_deadline_valid = false;
            net_cmd_t cmd = {
                .type = NET_CMD_TIMER_FIRED,
                .timer_generation = s_sm.timer_generation,
            };
            process_command(&cmd);
            did_work = true;
        }

        // RECONFIGURING timeout via elapsed time (no second timer).
        if (s_sm.state == NET_SM_STATE_RECONFIGURING) {
            if (!s_reconfig_deadline_valid) {
                s_reconfig_started_ms = now_ms;
                s_reconfig_deadline_ms = now_ms + NET_SM_RECONFIGURING_TIMEOUT_MS;
                s_reconfig_deadline_valid = true;
                s_sm.reconfig_start_ms = now_ms;
            } else if (deadline_due(now_ms, s_reconfig_deadline_ms)) {
                uint32_t elapsed = now_ms - s_reconfig_started_ms;
                ESP_LOGW(TAG, "RECONFIGURING timeout (%" PRIu32 " ms)", elapsed);
                s_reconfig_deadline_valid = false;
                s_sm.reconfig_start_ms = 0;
                net_cmd_t cmd = { .type = NET_CMD_RECONFIG_TIMEOUT };
                process_command(&cmd);
                did_work = true;
            }
        } else {
            s_reconfig_deadline_valid = false;
            s_sm.reconfig_start_ms = 0;
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

        now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t wait_ms = network_next_wait_ms(now_ms);
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        if (wait_ms > 0U && wait_ticks == 0) wait_ticks = 1;
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
    }
}
