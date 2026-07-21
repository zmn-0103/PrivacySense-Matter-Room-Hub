// PrivacySense Matter Room Hub - env_alert_sm.c
//
// Pure-logic implementation. See env_alert_sm.h for the state model and
// precedence rules. This file MUST NOT depend on FreeRTOS, ESP-IDF, SNTP,
// or any time() / clock() API — all inputs are caller-supplied.

#include "env_alert_sm.h"

void env_alert_sm_init(env_alert_sm_t *sm, env_alert_state_t initial)
{
    sm->state           = initial;
    sm->timer_start_ms  = 0;
    sm->timer_active    = false;
    sm->counting_alert  = false;
}

// Reset the confirm/clear timer without changing sm->state.
static void env_alert_reset_timer(env_alert_sm_t *sm)
{
    sm->timer_active   = false;
    sm->timer_start_ms = 0;
    sm->counting_alert = false;
}

bool env_alert_sm_eval(env_alert_sm_t *sm,
                       const env_alert_input_t *input,
                       bool occupancy_vacant,
                       bool occupancy_unknown,
                       uint32_t now_ms,
                       const env_alert_config_t *cfg)
{
    env_alert_state_t prev = sm->state;

    // ── 1. VACANT immediately clears (state-model.md §4.5) ──
    if (occupancy_vacant) {
        env_alert_reset_timer(sm);
        sm->state = ENV_ALERT_OK;
        return prev != sm->state;
    }

    // ── 2. UNKNOWN occupancy OR sensor offline → freeze (§4.4, §4.5) ──
    // State preserved; timer reset so resuming conditions must build a fresh
    // confirm/clear cycle. This avoids a brief outage near a threshold
    // boundary causing an immediate transition on resume.
    if (occupancy_unknown || !input->sensor_valid) {
        env_alert_reset_timer(sm);
        return false;
    }

    // ── 3. OCCUPIED + sensor online: threshold evaluation with hysteresis ──
    bool temp_above_alert  = input->temp_cc  > cfg->temp_alert_cc;
    bool humid_above_alert = input->humid_permil > cfg->humid_alert_permil;
    bool temp_below_clear  = input->temp_cc  < cfg->temp_clear_cc;
    bool humid_below_clear = input->humid_permil < cfg->humid_clear_permil;

    bool alert_condition = temp_above_alert  || humid_above_alert;
    bool clear_condition = temp_below_clear  && humid_below_clear;

    bool should_count_alert;
    if (alert_condition) {
        should_count_alert = true;
    } else if (clear_condition) {
        should_count_alert = false;
    } else {
        // Hysteresis band (between clear and alert on at least one metric):
        // keep current state, reset timer to prevent chatter-driven commits.
        env_alert_reset_timer(sm);
        return false;
    }

    // Start or restart timer on direction change.
    if (!sm->timer_active || sm->counting_alert != should_count_alert) {
        sm->timer_start_ms = now_ms;
        sm->timer_active   = true;
        sm->counting_alert = should_count_alert;
    }

    // Unsigned subtraction is wrap-around safe for any total elapsed
    // period < 2^32 ms (~49.7 days).
    uint32_t elapsed_ms = now_ms - sm->timer_start_ms;
    uint32_t required_ms = should_count_alert ? cfg->alert_confirm_ms
                                              : cfg->alert_clear_ms;
    if (elapsed_ms < required_ms) {
        return false;
    }

    // Transition committed.
    sm->state          = should_count_alert ? ENV_ALERT_ACTIVE : ENV_ALERT_OK;
    sm->timer_active   = false;
    sm->timer_start_ms = 0;
    sm->counting_alert = false;
    return prev != sm->state;
}
