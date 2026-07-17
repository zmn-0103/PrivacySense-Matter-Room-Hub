// PrivacySense Matter Room Hub - room_state.h
//
// Shared room state structure. Mirrors docs/state-model.md section 7.
//
// Ownership (docs/task-architecture.md §6.1):
//   - Writer: state_machine_task (only task that may mutate fields)
//   - Readers: ui_task, matter_adapter_task, network_task
//   - Synchronization: room_state_mutex (FreeRTOS Mutex, hold ≤ 10 ms)
//
// MUST NOT be modified by any task other than state_machine_task. Readers
// take a mutex-guarded snapshot; never access fields directly from another task.

#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Dimension 1: Occupancy (state-model.md §2) ---
typedef enum {
    OCCUPANCY_VACANT = 0,
    OCCUPANCY_OCCUPIED,
    OCCUPANCY_UNKNOWN
} occupancy_state_t;

// --- Dimension 2: User Mode (state-model.md §3) ---
typedef enum {
    MODE_NORMAL = 0,
    MODE_QUIET,
    MODE_NIGHT
} user_mode_t;

// --- Dimension 3: Env Alert (state-model.md §4) ---
typedef enum {
    ALERT_OK = 0,
    ALERT_ACTIVE
} env_alert_t;

// --- Combined room state (state-model.md §7) ---
typedef struct {
    occupancy_state_t occupancy;
    user_mode_t       user_mode;
    user_mode_t       pre_night_mode;   // Mode before NIGHT auto-switch
    env_alert_t       env_alert;
    bool              quiet_active;     // User toggled QUIET via button/Matter
    bool              wifi_connected;
    bool              matter_commissioned;
    bool              radar_online;
    bool              env_sensor_online;
} room_state_t;

// --- Lifecycle ---
// Called once from app_main before any task that touches room_state starts.
esp_err_t room_state_init(void);

// Take a mutex-guarded snapshot of the current room state.
// Returns ESP_OK on success, ESP_ERR_TIMEOUT if mutex not acquired within
// ROOM_STATE_SNAPSHOT_TIMEOUT_MS.
esp_err_t room_state_snapshot(room_state_t *out);

// Internal use by state_machine_task only. Acquires the mutex, updates fields
// from `src`, releases mutex. Other tasks MUST NOT call this.
esp_err_t room_state_update(const room_state_t *src);

// For diagnostics / uxTaskGetStackHighWaterMark polling by state_machine_task.
extern SemaphoreHandle_t g_room_state_mutex;

#ifdef __cplusplus
}
#endif
