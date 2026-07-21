// PrivacySense Matter Room Hub - night_window_sm.c
//
// Pure-logic implementation. See night_window_sm.h for window semantics.
// This file MUST NOT depend on FreeRTOS, ESP-IDF, SNTP, or any time() /
// clock() API — all inputs are caller-supplied.

#include "night_window_sm.h"

// Compute whether `local_min` falls inside [start, end).
//   start == end : empty window, never in-window (fixed semantics).
//   start <  end : normal window, in-window = (min >= start && min < end).
//   start >  end : cross-midnight, in-window = (min >= start || min < end).
static bool in_night_window(uint16_t local_min, uint16_t start, uint16_t end)
{
    if (start == end) {
        return false;
    }
    if (start < end) {
        return local_min >= start && local_min < end;
    }
    // Cross-midnight: [start, 1440) ∪ [0, end)
    return local_min >= start || local_min < end;
}

bool night_window_sm_eval(night_window_sm_state_t *st,
                          const night_window_time_t *t,
                          const night_window_config_t *cfg)
{
    // Time invalid: only act if currently NIGHT (exit to pre_night_mode).
    // If not NIGHT, leave everything alone (no entry without valid time).
    if (!t->time_valid) {
        if (st->user_mode == NIGHT_WINDOW_MODE_NIGHT) {
            st->user_mode = st->pre_night_mode;
            // pre_night_mode intentionally preserved: if SNTP recovers and
            // the window is still active, re-entering NIGHT will overwrite
            // it; if not, the user's pre-NIGHT mode is what we restore.
            return true;
        }
        return false;
    }

    bool in_window = in_night_window(t->local_minute,
                                     cfg->night_start_min,
                                     cfg->night_end_min);

    if (in_window) {
        if (st->user_mode != NIGHT_WINDOW_MODE_NIGHT) {
            st->pre_night_mode = st->user_mode;
            st->user_mode      = NIGHT_WINDOW_MODE_NIGHT;
            return true;
        }
        return false;
    }

    // Outside window: only act if currently NIGHT.
    if (st->user_mode == NIGHT_WINDOW_MODE_NIGHT) {
        st->user_mode = st->pre_night_mode;
        return true;
    }
    return false;
}
