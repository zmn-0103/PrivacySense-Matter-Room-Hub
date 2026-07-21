// PrivacySense Matter Room Hub - night_window_sm.h
//
// Platform-independent pure logic for the NIGHT window state machine
// (state-model.md §3). No FreeRTOS / ESP-IDF / SNTP / time() calls — the
// caller supplies {time_valid, local_minute}, making this unit host-testable
// without any hardware shim.
//
// Window semantics (state-model.md §3.5):
//   - Window is [start, end) in minutes since 00:00 (0..1439).
//   - Cross-midnight window: start > end (e.g. 22:00 = 1320, 07:00 = 420).
//     In-window = (min >= start) || (min < end).
//   - Normal window: start < end.
//     In-window = (min >= start) && (min < end).
//   - Empty window: start == end. Fixed semantics = NEVER in window
//     (documented and tested). This avoids the ambiguity where start == end
//     could be read as "always in window" by the cross-midnight formula.
//
// Transitions:
//   !time_valid  + in NIGHT  -> exit NIGHT, restore pre_night_mode
//   !time_valid  + not NIGHT -> no-op
//   time in window + not NIGHT -> save current mode as pre_night_mode, enter NIGHT
//   time in window + NIGHT     -> no-op
//   time outside    + NIGHT     -> exit NIGHT, restore pre_night_mode
//   time outside    + not NIGHT -> no-op
//
// Short-press handling within NIGHT (toggle quiet_active, keep MODE_NIGHT,
// update pre_night_mode to reflect user's post-NIGHT choice) is owned by
// mode_transition.c, NOT by this module. This module only auto-transitions
// based on the time window.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local mode enum to keep this header portable. Values MUST match
// room_state.h::user_mode_t so the integration layer can cast directly.
typedef enum {
    NIGHT_WINDOW_MODE_NORMAL = 0,
    NIGHT_WINDOW_MODE_QUIET,
    NIGHT_WINDOW_MODE_NIGHT
} night_window_mode_t;

typedef struct {
    night_window_mode_t user_mode;
    night_window_mode_t pre_night_mode;
    bool                quiet_active;
} night_window_sm_state_t;

typedef struct {
    bool     time_valid;
    uint16_t local_minute;   // 0..1439, only meaningful when time_valid
} night_window_time_t;

typedef struct {
    uint16_t night_start_min;   // 0..1439
    uint16_t night_end_min;     // 0..1439
} night_window_config_t;

// Returns true if user_mode changed.
// On enter NIGHT: pre_night_mode = old user_mode, user_mode = NIGHT.
// On exit  NIGHT: user_mode = pre_night_mode (pre_night_mode left as-is so
//                 a subsequent re-entry does not lose the user's prior mode).
// quiet_active is never modified by this function.
bool night_window_sm_eval(night_window_sm_state_t *st,
                          const night_window_time_t *t,
                          const night_window_config_t *cfg);

#ifdef __cplusplus
}
#endif
