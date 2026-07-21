#pragma once

#include <stdbool.h>

#include "room_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    user_mode_t user_mode;
    bool        quiet_active;
    user_mode_t pre_night_mode;
} mode_transition_result_t;

mode_transition_result_t mode_transition_short_press(user_mode_t current_mode,
                                                      bool quiet_active,
                                                      user_mode_t pre_night_mode);

#ifdef __cplusplus
}
#endif
