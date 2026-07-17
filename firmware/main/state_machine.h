// PrivacySense Matter Room Hub - state_machine.h
//
// Central state evaluator. Consumes the unified app_event_queue, computes the
// three parallel state dimensions (state-model.md §1), and updates room_state.
//
// Task profile (task-architecture.md §4.3):
//   - Priority 6 (highest business priority)
//   - Stack   6144 B
//   - Trigger event-driven (app_event_queue) + 1 s periodic (NIGHT window)
//   - Watchdog feed every loop iteration (≤ 1 s gap), TWDT timeout 10 s
//
// Event flow (task-architecture.md §3, §5):
//   All producers (radar/env/button/network/matter_adapter) push app_event_t
//   into g_app_event_queue. This task is the ONLY consumer.
//
//   state_machine_task also pushes matter_report_t into g_matter_report_queue
//   (consumed by matter_adapter_task) and config_event_t into
//   g_config_event_queue (consumed by config_task).

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "room_state.h"
#include "config.h"
#include "ld2410c.h"          // ld2410c_radar_data_t
#include "env_sensor.h"       // env_sensor_data_t

#ifdef __cplusplus
extern "C" {
#endif

// --- Input event types (task-architecture.md §5.2) ---
typedef enum {
    EVENT_RADAR_DATA = 0,        // 雷达占用数据
    EVENT_ENV_DATA,              // 环境传感器数据（DHT22）
    EVENT_BUTTON,                // 按键事件（消抖后）
    EVENT_NETWORK_STATUS,        // 网络状态变化
    EVENT_MATTER_COMMAND,        // Matter 命令（如 ChangeToMode）
    EVENT_MATTER_READ,           // Matter 属性读取请求
    EVENT_CONFIG_CHANGE,         // 配置变更
    EVENT_TIMER_1S,              // 1 秒定时器超时
} app_event_type_t;

// --- Debounced button events (produced by button_task) ---
typedef enum {
    BUTTON_EVENT_SHORT_PRESS = 0,
    BUTTON_EVENT_LONG_PRESS
} button_event_t;

// --- Network status (produced by network_task) ---
typedef enum {
    NETWORK_STATUS_DISCONNECTED = 0,
    NETWORK_STATUS_CONNECTED,
    NETWORK_STATUS_PROVISIONED,    // Credentials received, connecting
} network_status_t;

// --- Matter commands (produced by matter_adapter_task from CHIP callbacks) ---
typedef enum {
    MATTER_COMMAND_CHANGE_TO_MODE = 0,   // ModeSelect ChangeToMode(newMode)
} matter_command_type_t;

typedef struct {
    matter_command_type_t type;
    uint8_t               new_mode;       // 0=NORMAL, 1=QUIET, 2=NIGHT
    void                 *cmd_ctx;        // Opaque CHIP command context
    void                *resp_handle;     // Opaque handle for async response
} matter_command_t;

// --- Unified event payload ---
typedef struct {
    app_event_type_t type;
    union {
        ld2410c_radar_data_t radar;
        env_sensor_data_t    env;
        button_event_t       button;
        network_status_t     network;
        matter_command_t     matter_cmd;
    } data;
    uint32_t timestamp_ms;
} app_event_t;

// --- Matter report (state_machine_task → matter_adapter_task) ---
// Only carries occupancy / CurrentMode changes + reconnect force-sync.
// Never carries raw sensor data (matter-data-model.md §6).
typedef enum {
    MATTER_REPORT_OCCUPANCY = 0,
    MATTER_REPORT_CURRENT_MODE,
    MATTER_REPORT_FORCE_SYNC,    // Wi-Fi reconnected: re-report all attributes
} matter_report_type_t;

typedef struct {
    matter_report_type_t type;
    // Snapshot of room_state fields relevant to Matter.
    occupancy_state_t occupancy;
    user_mode_t       user_mode;
} matter_report_t;

// --- Lifecycle ---
esp_err_t state_machine_init(void);

// state_machine_task entry point. Created by app_main with stack 6144, prio 6.
void state_machine_task(void *pvParameters);

// --- Queues (all created by state_machine_init) ---
// app_event_queue: depth 32, single consumer = state_machine_task.
// matter_report_queue: depth 8, single consumer = matter_adapter_task.
// config_event_queue: depth 4, single consumer = config_task (defined in config.h).
extern QueueHandle_t g_app_event_queue;
extern QueueHandle_t g_matter_report_queue;

#ifdef __cplusplus
}
#endif
