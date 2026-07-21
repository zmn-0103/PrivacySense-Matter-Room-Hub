#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OCC_VACANT = 0,
    OCC_OCCUPIED,
    OCC_UNKNOWN
} occ_state_t;

typedef struct {
    occ_state_t state;
    uint32_t    timer_start_ms;
    bool        timer_active;
    bool        counting_occupy;
} occ_sm_t;

void occ_sm_init(occ_sm_t *sm, occ_state_t initial);

bool occ_sm_eval(occ_sm_t *sm, bool occupied, uint32_t now_ms,
                 uint32_t entry_confirm_ms, uint32_t exit_delay_ms);

#ifdef __cplusplus
}
#endif
