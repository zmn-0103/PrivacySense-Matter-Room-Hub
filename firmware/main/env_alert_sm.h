// PrivacySense Matter Room Hub - env_alert_sm.h
//
// Platform-independent pure logic for the Environment Alert state machine
// (state-model.md §4). No FreeRTOS / ESP-IDF / SNTP / time() calls — all
// time and sensor data is passed in by the caller, making this unit
// host-testable without any hardware shim.
//
// State model:
//   OK     -- all metrics within normal range
//   ACTIVE -- at least one metric above alert threshold for confirm duration
//
// Transitions (state-model.md §4.4):
//   OK      --(any metric > alert thr for alert_confirm_ms)-->  ACTIVE
//   ACTIVE  --(all metrics < clear thr for alert_clear_ms)-->  OK
//   ACTIVE  --(occupancy becomes VACANT)----------------------> OK (immediate)
//   any     --(occupancy UNKNOWN OR sensor offline)---------->  freeze
//
// Hysteresis: the band between clear and alert thresholds keeps the current
// state and resets the confirm/clear timer, preventing boundary chatter.
//
// Timer safety: elapsed time is computed as (now_ms - timer_start_ms) using
// unsigned arithmetic, which is wrap-around safe as long as the total
// elapsed period is less than 2^32 ms (~49.7 days).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENV_ALERT_OK = 0,
    ENV_ALERT_ACTIVE
} env_alert_state_t;

typedef struct {
    env_alert_state_t state;
    uint32_t          timer_start_ms;
    bool              timer_active;
    bool              counting_alert;   // true: counting toward ACTIVE; false: toward OK
} env_alert_sm_t;

// Sensor sample for one evaluation. `sensor_valid=false` forces a freeze
// regardless of temperature/humidity values.
typedef struct {
    int16_t  temp_cc;         // centi-celsius (1/100 °C)
    uint16_t humid_permil;    // per-mille (0..1000)
    bool     sensor_valid;    // false if env sensor offline / invalid sample
} env_alert_input_t;

typedef struct {
    int16_t  temp_alert_cc;       // alert threshold (e.g. 3200 = 32.00 °C)
    int16_t  temp_clear_cc;       // clear threshold  (e.g. 3000 = 30.00 °C)
    uint16_t humid_alert_permil;  // alert threshold (e.g. 750 = 75.0 %RH)
    uint16_t humid_clear_permil;  // clear threshold  (e.g. 700 = 70.0 %RH)
    uint32_t alert_confirm_ms;    // confirm duration before entering ACTIVE
    uint32_t alert_clear_ms;      // clear duration before returning to OK
} env_alert_config_t;

void env_alert_sm_init(env_alert_sm_t *sm, env_alert_state_t initial);

// Evaluate one step.
//
// Precedence (state-model.md §4.4, §4.5):
//   1. occupancy_vacant == true  -> immediately OK (timer reset)
//   2. occupancy_unknown == true OR input->sensor_valid == false -> freeze
//      (state preserved, timer reset so a full confirm/clear cycle is
//       required when conditions resume — prevents boundary exploits)
//   3. otherwise (OCCUPIED + sensor online):
//        - any metric above alert threshold  -> count toward ACTIVE
//        - all metrics below clear threshold -> count toward OK
//        - hysteresis band                   -> keep state, reset timer
//
// Returns true if sm->state changed. The struct is always updated (timer
// progress committed) so the caller SHOULD persist it on every call,
// matching the occ_sm candidate pattern.
bool env_alert_sm_eval(env_alert_sm_t *sm,
                       const env_alert_input_t *input,
                       bool occupancy_vacant,
                       bool occupancy_unknown,
                       uint32_t now_ms,
                       const env_alert_config_t *cfg);

#ifdef __cplusplus
}
#endif
