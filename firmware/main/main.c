// PrivacySense Matter Room Hub - application entry point
//
// Boots the system, initialises all modules and spawns the FreeRTOS tasks
// defined in docs/task-architecture.md §2. The app_main task itself returns
// after spawn; FreeRTOS idle task takes over CPU0.
//
// Stack high-water marks are queried via uxTaskGetStackHighWaterMark() inside
// each task's own loop and logged at INFO level. ESP-IDF returns the value
// in BYTES (not words) on ESP32-C6, so all logs are labelled "bytes"; revisit
// if any task shows less than 256 bytes of headroom (task-architecture.md §4).
//
// Build / flash / monitor (inside WSL2 Ubuntu 24.04 with esp-idf + esp-matter
// env loaded — see docs/development-workflow.md):
//   cd /home/projects/PrivacySense-Matter-Room-Hub/firmware
//   idf.py set-target esp32c6
//   idf.py build
//   idf.py -p /dev/ttyUSB0 flash monitor

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pins.h"
#include "room_state.h"
#include "config.h"
#include "state_machine.h"
#include "button.h"
#include "ui.h"
#include "network.h"
#include "matter_app.h"

#include "ld2410c.h"
#include "env_sensor.h"
#include "radar_diag.h"
#include "network_diag.h"
#include "esp_console.h"

static const char *TAG = "main";

// --- Task parameters (task-architecture.md §2) ---
#define TASK_STATE_MACHINE_STACK   6144
#define TASK_STATE_MACHINE_PRIO    6
#define TASK_BUTTON_STACK          3072
#define TASK_BUTTON_PRIO           5
#define TASK_UI_STACK              3072
#define TASK_UI_PRIO               3
#define TASK_NETWORK_STACK         8192
#define TASK_NETWORK_PRIO          4
#define TASK_MATTER_STACK          12288
#define TASK_MATTER_PRIO           4
#define TASK_CONFIG_STACK          4096
#define TASK_CONFIG_PRIO           2

// --- Sensor → app_event_queue bridges ---
// ld2410c and env_sensor are callback-based (they do NOT hold any queue
// handle). main.c registers these thin wrappers that wrap the data in an
// app_event_t and forward to the unified g_app_event_queue (the only queue
// consumed by state_machine_task).

static void radar_data_handler(const ld2410c_radar_data_t *data)
{
    static uint32_t s_radar_drop_count = 0;

    app_event_t ev = {
        .type         = EVENT_RADAR_DATA,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
    ev.data.radar = *data;
    if (xQueueSend(g_app_event_queue, &ev, 0) != pdTRUE) {
        s_radar_drop_count++;
        if ((s_radar_drop_count % 60) == 1) {
            ESP_LOGW(TAG, "radar data dropped %u times (last valid=%d)",
                     (unsigned)s_radar_drop_count, data->valid);
        }
    }
}

static void env_data_handler(const env_sensor_data_t *data)
{
    static uint32_t s_env_drop_count = 0;

    app_event_t ev = {
        .type         = EVENT_ENV_DATA,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
    ev.data.env = *data;
    if (xQueueSend(g_app_event_queue, &ev, 0) != pdTRUE) {
        s_env_drop_count++;
        if ((s_env_drop_count % 60) == 1) {
            ESP_LOGW(TAG, "env data dropped %u times (last valid=%d)",
                     (unsigned)s_env_drop_count, data->valid);
        }
    }
}

static void log_init_banner(void)
{
    ESP_LOGI(TAG, "=== PrivacySense Matter Room Hub ===");
    ESP_LOGI(TAG, "target: esp32c6, ESP-IDF v5.4.1, esp-matter release/v1.5");
    ESP_LOGI(TAG, "build summary / commit / upstream warnings: see firmware/README.md");
}

// Initialise the DEFAULT NVS partition (Wi-Fi/Matter/BLE credentials).
// Business config lives in a SEPARATE partition "ps_cfg" and is initialised
// by config_init() — corruption of the default partition does NOT affect it.
//
// If the default partition is corrupt, erase ONLY the default partition and
// re-init. This loses Wi-Fi/Matter/BLE credentials and triggers the
// re-commissioning flow (commissioning-lifecycle.md §3), but business config
// (state-model.md §8) survives. This is the explicit recovery path, not a
// silent start-up repair.
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "Default NVS partition corrupt (%s); erasing DEFAULT "
                 "partition only. Wi-Fi/Matter/BLE credentials WILL be lost → "
                 "device will enter re-commissioning. Business config in "
                 "ps_cfg partition is preserved.",
                 esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());   // erases default partition only
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "default NVS initialised (ps_cfg partition handled by config_init)");
}

void app_main(void)
{
    log_init_banner();

    // NVS first — every other module depends on it.
    init_nvs();

    // Shared state + config before anything that reads them.
    ESP_ERROR_CHECK(room_state_init());
    ESP_ERROR_CHECK(config_init());
    ESP_ERROR_CHECK(state_machine_init());   // creates g_app_event_queue + g_matter_report_queue

    // Peripherals + drivers.
    ESP_ERROR_CHECK(button_init());

    // UI is a NON-critical peripheral (state-model.md §6, AGENTS.md §3
    // "本地可用性"). A broken on-board WS2812 or RMT channel MUST NOT prevent
    // sensors / state machine / network / Matter from running. ui_init() now
    // releases all resources on failure and returns a non-OK esp_err_t; we
    // log loudly but continue boot. ui_task is spawned conditionally below;
    // all UI public APIs (ui_set_long_press_countdown etc.) are safe no-ops
    // when UI is degraded, so button.c does not need to change.
    {
        esp_err_t uret = ui_init();
        if (uret != ESP_OK) {
            ESP_LOGW(TAG, "ui_init failed: %s — continuing WITHOUT UI "
                     "(LED off; long-press countdown will not render)",
                     esp_err_to_name(uret));
        }
    }

    ESP_ERROR_CHECK(network_init());

    // Sensor components expose a callback-based start API; they emit
    // app_event_t into g_app_event_queue via the handlers above.
    ESP_ERROR_CHECK(ld2410c_start(PIN_RADAR_UART_NUM,
                                   PIN_RADAR_UART_TX_GPIO,
                                   PIN_RADAR_UART_RX_GPIO,
                                   PIN_RADAR_UART_BAUD,
                                   radar_data_handler));

    ESP_ERROR_CHECK(env_sensor_start(PIN_DHT22_DATA_GPIO,
                                      PIN_DHT22_RMT_CLK_HZ,
                                      env_data_handler));

    // Matter last — depends on Wi-Fi + NVS being ready.
    // matter_app_init() currently returns ESP_ERR_NOT_SUPPORTED because the
    // esp_matter node/endpoint creation is a follow-up TODO. Treat that
    // specific code as "Matter intentionally stubbed, continue boot" and log
    // it loudly so the operator knows Matter is offline in this build. Any
    // OTHER error aborts via ESP_ERROR_CHECK.
    {
        esp_err_t mret = matter_app_init();
        if (mret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "matter_app_init: STUB return (Matter offline, "
                     "boot continues; matter-data-model.md §2 TODO)");
        } else {
            ESP_ERROR_CHECK(mret);
        }
    }

    // --- Spawn application tasks ---
    // sensor_radar_task and sensor_env_task are spawned inside their
    // respective components (ld2410c_start / env_sensor_start).
    // Each xTaskCreate result is checked: a failed spawn leaves the device in
    // a partial-running state which is worse than a clean abort, so panic on
    // failure. Stack/prio constants come from task-architecture.md §2.
    #define SPAWN_TASK(name, fn, stack, prio)                              \
        do {                                                               \
            if (xTaskCreate(fn, name, stack, NULL, prio, NULL) != pdPASS) { \
                ESP_LOGE(TAG, "xTaskCreate(%s) failed; aborting", name);   \
                ESP_ERROR_CHECK(ESP_FAIL);                                 \
            }                                                              \
            ESP_LOGI(TAG, "task %s spawned (stack=%u, prio=%d)",           \
                     name, (unsigned)(stack), (int)(prio));                \
        } while (0)

    SPAWN_TASK("state_machine", state_machine_task,
               TASK_STATE_MACHINE_STACK, TASK_STATE_MACHINE_PRIO);
    SPAWN_TASK("button",        button_task,
               TASK_BUTTON_STACK, TASK_BUTTON_PRIO);
    // Spawn ui_task ONLY if ui_init() succeeded. When UI is degraded, button
    // long-press countdown writes are silently dropped by ui_set_long_press_
    // countdown() and the rest of the system continues normally.
    if (ui_is_initialized()) {
        SPAWN_TASK("ui",        ui_task,
                   TASK_UI_STACK, TASK_UI_PRIO);
    } else {
        ESP_LOGW(TAG, "skipping ui_task spawn (UI degraded)");
    }
    SPAWN_TASK("network",       network_task,
               TASK_NETWORK_STACK, TASK_NETWORK_PRIO);
    SPAWN_TASK("matter_adapt",  matter_adapter_task,
               TASK_MATTER_STACK, TASK_MATTER_PRIO);
    SPAWN_TASK("config",        config_task,
               TASK_CONFIG_STACK, TASK_CONFIG_PRIO);

    #undef SPAWN_TASK

    // Radar diagnostics console (R11 testing). No abort on failure.
    {
        esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
        esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
        esp_console_repl_t *repl = NULL;
        esp_err_t r = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
        if (r == ESP_OK) {
            radar_diag_register();
            #ifdef CONFIG_NETWORK_DIAG_CONSOLE
            network_diag_register();
            #endif
            esp_console_register_help_command();
            esp_console_start_repl(repl);
        } else {
            ESP_LOGW(TAG, "console REPL init: %s — no interactive radar diag",
                     esp_err_to_name(r));
        }
    }

    ESP_LOGI(TAG, "all tasks spawned; app_main returning to idle");
    // app_main returns; FreeRTOS idle task runs on CPU0.
}
