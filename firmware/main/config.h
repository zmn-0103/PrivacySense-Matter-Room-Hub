// PrivacySense Matter Room Hub - config.h
//
// NVS-backed configuration parameters. Mirrors docs/state-model.md §8.
//
// All values are persisted in NVS under namespace "ps_cfg". Defaults are
// applied on first boot (NVS empty) or after factory reset. Runtime updates
// are queued via config_event_queue and applied by config_task.
//
// SECURITY: This module MUST NOT persist or log any of:
//   - Wi-Fi SSID / passphrase (managed by ESP-IDF Wi-Fi provisioning)
//   - Matter passcode / discriminator / setup payload
//   - Device MAC address

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- NVS partition + namespace + keys ---
// Business config lives in a DEDICATED NVS partition ("ps_cfg") so that
// corruption of the default `nvs` partition (Wi-Fi/Matter/BLE credentials)
// can be erased and recovered WITHOUT losing business config. See
// commissioning-lifecycle.md §3.3 and partitions.csv.
#define CONFIG_NVS_PARTITION   "ps_cfg"
#define CONFIG_NVS_NAMESPACE   "ps_cfg"
#define CONFIG_VERSION_CURRENT 1U

// --- Tunable parameters (state-model.md §8) ---
typedef struct {
    uint32_t entry_confirm_ms;   // Default 2000, range [500, 5000]
    uint32_t exit_delay_ms;      // Default 120000, range [30000, 600000]
    uint32_t sensor_timeout_ms;  // Default 10000, range [5000, 30000]
    uint32_t radar_eval_ms;      // Default 200, range [100, 500]
    // Night window: "HH:MM" stored as minutes since 00:00 (0..1439)
    uint16_t night_start_min;    // Default 22:00 = 1320
    uint16_t night_end_min;      // Default 07:00 = 420
    // Env thresholds — centi-celsius (1/100 °C) and per-mille (1/1000 %RH).
    int16_t  temp_alert_cc;      // Default 3200 (32.00 °C)
    int16_t  temp_clear_cc;      // Default 3000 (30.00 °C)
    uint16_t humid_alert_permil; // Default 750 (75.0 %RH) in per-mille
    uint16_t humid_clear_permil; // Default 700 (70.0 %RH)
    uint16_t co2_alert_ppm;      // Default 1000
    uint16_t co2_clear_ppm;      // Default 800
    uint16_t alert_confirm_s;    // Default 60
    uint16_t alert_clear_s;      // Default 120
    uint16_t config_version;     // Schema version for migrations
} ps_config_t;

// --- Config change events (consumed by config_task) ---
// config_task is the ONLY writer to s_cfg (single-writer rule per
// task-architecture.md §4.8). Callers of config_set() do NOT mutate s_cfg
// directly — they enqueue a CONFIG_EVENT_APPLY_NEW with a full ps_config_t
// copy; config_task validates, applies, and persists atomically.
typedef enum {
    CONFIG_EVENT_UPDATE_PARAM = 0,   // TODO: future per-key update (carries param id + value)
    CONFIG_EVENT_FACTORY_RESET,      // Erase ps_cfg namespace + apply defaults
    CONFIG_EVENT_PERSIST_NOW,        // Persist current s_cfg (e.g. after internal edit)
    CONFIG_EVENT_APPLY_NEW,          // Apply + persist a caller-supplied full config copy
} config_event_type_t;

typedef struct {
    config_event_type_t type;
    // Valid only when type == CONFIG_EVENT_APPLY_NEW. Owned by config_task
    // after dequeue; the caller MUST NOT mutate the pointer after sending.
    ps_config_t new_cfg;
} config_event_t;

// --- Async result notification (task-architecture.md §4.8 ack) ---
// config_task posts a config_result_t to g_config_result_queue after each
// APPLY_NEW attempt. Callers that need to confirm on-disk durability can
// listen on this queue (non-blocking peek or bounded wait). The queue is
// depth 2 — enough for one in-flight APPLY_NEW plus a second one that may
// arrive while the first is still being processed. A result is NOT posted
// for CONFIG_EVENT_PERSIST_NOW / FACTORY_RESET (those are internal paths
// with no external caller to ack).
typedef enum {
    CONFIG_RESULT_OK = 0,              // Validated + persisted + s_cfg updated
    CONFIG_RESULT_REJECTED,            // Validation failed (range / hysteresis / version)
    CONFIG_RESULT_PERSIST_FAILED,      // NVS write failed after bounded retries
    CONFIG_RESULT_MUTEX_TIMEOUT,       // s_cfg mutex could not be taken (flash updated, s_cfg stale)
} config_result_kind_t;

typedef struct {
    config_result_kind_t kind;
    esp_err_t            err;          // ESP_OK for OK/REJECTED; NVS error for PERSIST_FAILED
} config_result_t;

// --- Lifecycle ---
esp_err_t config_init(void);          // Load from NVS or apply defaults
esp_err_t config_get(ps_config_t *out);

// Enqueue a full config update. Returns:
//   ESP_OK            — update enqueued, will be applied + persisted by config_task
//   ESP_ERR_INVALID_ARG — new_cfg == NULL
//   ESP_ERR_NO_MEM    — config_event_queue full; s_cfg is UNCHANGED (no partial commit)
//   ESP_ERR_TIMEOUT   — queue send timed out (20 ms); s_cfg is UNCHANGED
//
// NOTE: a successful return means "queued for application", NOT "already on
// flash". The persistence happens asynchronously in config_task. Callers that
// need to confirm on-disk durability must receive a CONFIG_RESULT_OK from
// g_config_result_queue (see config_result_kind_t above).
esp_err_t config_set(const ps_config_t *new_cfg);

// Queue handle; state_machine_task / matter_adapter_task push events here.
// Owned by config_task. Depth = 4 (task-architecture.md §5.1).
extern QueueHandle_t g_config_event_queue;

// Async result queue. Owned by config_task. Depth = 2. Consumers (e.g.
// state_machine_task after issuing a config_set) can receive from this queue
// to learn whether the apply succeeded. A full result queue does NOT block
// config_task — it peeks-and-skips stale entries to make room.
extern QueueHandle_t g_config_result_queue;

// config_task entry point. Created by app_main with stack 4096, prio 2.
void config_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
