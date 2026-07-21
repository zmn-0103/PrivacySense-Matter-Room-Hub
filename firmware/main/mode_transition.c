#include "mode_transition.h"

mode_transition_result_t mode_transition_short_press(user_mode_t current_mode,
                                                      bool quiet_active,
                                                      user_mode_t pre_night_mode)
{
    mode_transition_result_t r;
    r.user_mode      = current_mode;
    r.quiet_active   = quiet_active;
    r.pre_night_mode = pre_night_mode;

    if (current_mode == MODE_NIGHT) {
        r.quiet_active = !quiet_active;
        r.pre_night_mode = quiet_active ? MODE_NORMAL : MODE_QUIET;
        // When quiet becomes true, NIGHT exit → QUIET; when quiet becomes false,
        // NIGHT exit → NORMAL. This gives the user a way to choose the post-NIGHT
        // mode without leaving NIGHT mode.
    } else if (quiet_active || current_mode == MODE_QUIET) {
        r.user_mode    = MODE_NORMAL;
        r.quiet_active = false;
    } else {
        r.user_mode    = MODE_QUIET;
        r.quiet_active = true;
    }
    return r;
}
