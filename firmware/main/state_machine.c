// PrivacySense Matter Room Hub - state_machine.c
//
// Skeleton implementation. Real logic for occupancy / mode / env-alert
// dimensions will be added in follow-up commits after first build succeeds.
//
// This file currently:
//   - Creates the unified app_event_queue (depth 32) and matter_report_queue
//     (depth 8)
//   - Runs the task loop with 1 s periodic wake (NIGHT window check)
//   - Feeds TWDT every iteration (≤ 1 s gap, TWDT 10 s)
//   - Logs queue occupancy for diagnostics

#include "state_machine.h"

#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "config.h"
#include "matter_app.h"
#include "room_state.h"

static const char *TAG = "state_machine";

#define APP_EVENT_QUEUE_DEPTH       32
#define MATTER_REPORT_QUEUE_DEPTH   8
#define STATE_MACHINE_QUEUE_TIMEOUT_MS 1000U   // task-architecture.md §4.3

QueueHandle_t g_app_event_queue       = NULL;
QueueHandle_t g_matter_report_queue   = NULL;

esp_err_t state_machine_init(void)
{
    if (g_app_event_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    g_app_event_queue = xQueueCreate(APP_EVENT_QUEUE_DEPTH, sizeof(app_event_t));
    g_matter_report_queue = xQueueCreate(MATTER_REPORT_QUEUE_DEPTH, sizeof(matter_report_t));

    if (!g_app_event_queue || !g_matter_report_queue) {
        ESP_LOGE(TAG, "queue creation failed (heap exhausted?)");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init ok (app_event depth=%d, matter_report depth=%d)",
             APP_EVENT_QUEUE_DEPTH, MATTER_REPORT_QUEUE_DEPTH);
    return ESP_OK;
}

static void process_radar(const ld2410c_radar_data_t *data)
{
    // TODO: occupancy dimension evaluation (state-model.md §2)
    //   - Apply ENTRY_CONFIRM_MS / EXIT_DELAY_MS / SENSOR_TIMEOUT_MS timers
    //   - On radar timeout → occupancy = UNKNOWN
    //   - UNKNOWN recovery: re-run entry/exit confirmation on the recovered
    //     frame (state-model.md §2.3); do NOT immediately jump to VACANT.
    //   - On confirmed VACANT↔OCCUPIED transition: push MATTER_REPORT_OCCUPANCY
    //     to g_matter_report_queue.
    (void)data;
}

static void process_env(const env_sensor_data_t *data)
{
    // TODO: env-alert dimension evaluation (state-model.md §4)
    //   - Hysteresis: temp/humid alert vs clear thresholds
    //   - Confirm time 60 s / clear time 120 s
    //   - Auto-clear ALERT when occupancy → VACANT (state-model.md §4.5)
    //   - DHT22 invalid range: temp < -40 or > 80 °C, humid < 0 or > 100 %RH
    //     → ignore sample; 5 consecutive invalid → env_sensor_online=false
    //   - Consecutive 3 read failures → env_sensor_online=false
    //   - env_alert is LOCAL ONLY (no Matter Endpoint) per matter-data-model.md §5
    (void)data;
}

static void process_button(button_event_t event)
{
    // TODO: user-mode dimension evaluation (state-model.md §3)
    //   - SHORT_PRESS → toggle QUIET (updates quiet_active; pushes
    //     MATTER_REPORT_CURRENT_MODE if user_mode changes)
    //   - LONG_PRESS  → factory reset / re-commissioning (commissioning-lifecycle.md)
    (void)event;
}

static void process_network(network_status_t status)
{
    // TODO: update room_state.wifi_connected (state-machine is the only writer).
    //   - On CONNECTED: push MATTER_REPORT_FORCE_SYNC to g_matter_report_queue
    //     (matter-data-model.md §6.3 — re-report occupancy then CurrentMode)
    (void)status;
}

static void process_matter_command(const matter_command_t *cmd)
{
    // TODO: route ChangeToMode into user-mode state machine (matter-data-model.md §4.4-4.6)
    //   - Validate new_mode ∈ SupportedModes (0/1/2)
    //   - NIGHT: allow only if local time is valid AND inside night window;
    //     otherwise return FAILURE (do NOT silently keep old state and return OK)
    //   - QUIET: apply immediately (user intent priority)
    //   - NORMAL: clear quiet_active
    //   - Wait ≤ 100 ms for the actual state-machine transition result, then
    //     call matter_app_respond_change_to_mode() with SUCCESS / FAILURE.
    //     Timeout (task-architecture.md §4.7) MUST NOT return SUCCESS.
    (void)cmd;
}

static void evaluate_night_window(uint32_t now_ms, const ps_config_t *cfg)
{
    // TODO: NIGHT window check every 60 s (state-model.md §3.5)
    //   - Time source priority: SNTP > (last_sync + uptime) > none
    //   - ESP32-C6 has NO battery-backed RTC; after power loss with SNTP not
    //     yet restored, time is unknown → disable NIGHT, rely on button only
    //   - Crossing into [night_start_min, night_end_min) → MODE_NIGHT
    //   - Crossing out → restore pre_night_mode (QUIET or NORMAL)
    (void)now_ms;
    (void)cfg;
}

void state_machine_task(void *pvParameters)
{
    (void)pvParameters;

    // Register with TWDT (task-architecture.md §7.1).
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ps_config_t cfg;
    if (config_get(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "config_get failed; running with defaults in-memory");
        memset(&cfg, 0, sizeof(cfg));
    }

    uint32_t loop_count = 0;
    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), uxTaskPriorityGet(NULL));

    for (;;) {
        app_event_t ev;
        // Block up to 1 s waiting for events; periodic wake feeds TWDT and
        // runs the NIGHT window check.
        if (xQueueReceive(g_app_event_queue, &ev,
                          pdMS_TO_TICKS(STATE_MACHINE_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            switch (ev.type) {
            case EVENT_RADAR_DATA:
                process_radar(&ev.data.radar);
                break;
            case EVENT_ENV_DATA:
                process_env(&ev.data.env);
                break;
            case EVENT_BUTTON:
                process_button(ev.data.button);
                break;
            case EVENT_NETWORK_STATUS:
                process_network(ev.data.network);
                break;
            case EVENT_MATTER_COMMAND:
                process_matter_command(&ev.data.matter_cmd);
                break;
            case EVENT_MATTER_READ:
            case EVENT_CONFIG_CHANGE:
            case EVENT_TIMER_1S:
                // TODO: handle remaining event types
                break;
            default:
                ESP_LOGW(TAG, "unknown event type %d", ev.type);
                break;
            }
        }

        // Periodic check (every 1 s wake) for NIGHT window transitions.
        evaluate_night_window(xTaskGetTickCount() * portTICK_PERIOD_MS, &cfg);

        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop_count % 60) == 0) {
            ESP_LOGI(TAG, "heartbeat: loop=%u, stack_hwm=%u bytes",
                     (unsigned)loop_count,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}
