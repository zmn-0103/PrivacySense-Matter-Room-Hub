#include "occ_sm.h"

void occ_sm_init(occ_sm_t *sm, occ_state_t initial)
{
    sm->state = initial;
    sm->timer_start_ms = 0;
    sm->timer_active = false;
    sm->counting_occupy = true;
}

bool occ_sm_eval(occ_sm_t *sm, bool occupied, uint32_t now_ms,
                 uint32_t entry_confirm_ms, uint32_t exit_delay_ms)
{
    occ_state_t prev = sm->state;

    if (prev == OCC_UNKNOWN) {
        if (!sm->timer_active) {
            sm->timer_start_ms = now_ms;
            sm->timer_active = true;
            sm->counting_occupy = occupied;
        } else if (occupied != sm->counting_occupy) {
            sm->timer_start_ms = now_ms;
            sm->timer_active = true;
            sm->counting_occupy = occupied;
        }

        uint32_t required = sm->counting_occupy ? entry_confirm_ms : exit_delay_ms;
        if ((now_ms - sm->timer_start_ms) < required) {
            return false;
        }

        sm->state = sm->counting_occupy ? OCC_OCCUPIED : OCC_VACANT;
        sm->timer_start_ms = 0;
        sm->timer_active = false;
        return true;
    }

    if (prev == OCC_VACANT) {
        if (occupied) {
            if (!sm->timer_active) {
                sm->timer_start_ms = now_ms;
                sm->timer_active = true;
            }
            if ((now_ms - sm->timer_start_ms) >= entry_confirm_ms) {
                sm->state = OCC_OCCUPIED;
                sm->timer_start_ms = 0;
                sm->timer_active = false;
                return true;
            }
        } else {
            sm->timer_start_ms = 0;
            sm->timer_active = false;
        }
        return false;
    }

    if (!occupied) {
        if (!sm->timer_active) {
            sm->timer_start_ms = now_ms;
            sm->timer_active = true;
        }
        if ((now_ms - sm->timer_start_ms) >= exit_delay_ms) {
            sm->state = OCC_VACANT;
            sm->timer_start_ms = 0;
            sm->timer_active = false;
            return true;
        }
    } else {
        sm->timer_start_ms = 0;
        sm->timer_active = false;
    }
    return false;
}
