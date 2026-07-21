#include "ui_rgb.h"

void ui_rgb_compute(const room_state_t *st, uint32_t now_ms, ui_rgb_output_t *out)
{
    out->r = 0;
    out->g = 0;
    out->b = 0;
    out->pattern = RGB_PATTERN_OFF;

    if (!st->radar_online || !st->env_sensor_online) {
        bool on = (now_ms % 1000) < 500;
        if (!st->env_sensor_online && (now_ms % 10000) < 200) {
            out->r = 63;
            out->g = 63;
            out->pattern = RGB_PATTERN_RED_YELLOW_BLIP;
        } else {
            out->r = on ? 64 : 0;
            out->pattern = RGB_PATTERN_RED_BLINK;
        }
        out->priority = 1;
    } else if (st->commissioning_active) {
        bool on = (now_ms % 500) < 250;
        out->b = on ? 32 : 0;
        out->pattern = RGB_PATTERN_BLUE_FAST_BLINK;
        out->priority = 2;
    } else if (!st->wifi_connected) {
        bool on = (now_ms % 2000) < 1000;
        out->r = out->g = out->b = on ? 16 : 0;
        out->pattern = RGB_PATTERN_WHITE_SLOW_BLINK;
        out->priority = 3;
    } else if (st->env_alert == ALERT_ACTIVE) {
        out->r = 127;
        out->g = 127;
        out->pattern = RGB_PATTERN_YELLOW_STEADY;
        out->priority = 4;
    } else if (st->occupancy == OCCUPANCY_OCCUPIED
               && st->user_mode == MODE_NIGHT) {
        out->r = 25;
        out->g = 18;
        out->b = 5;
        out->pattern = RGB_PATTERN_WARM_WHITE;
        out->priority = 5;
    } else if (st->occupancy == OCCUPANCY_OCCUPIED
               && (st->user_mode == MODE_QUIET || st->quiet_active)) {
        out->b = 51;
        out->pattern = RGB_PATTERN_BLUE_LOW;
        out->priority = 6;
    } else if (st->occupancy == OCCUPANCY_OCCUPIED) {
        out->g = 127;
        out->pattern = RGB_PATTERN_GREEN;
        out->priority = 7;
    } else if (st->occupancy == OCCUPANCY_UNKNOWN) {
        out->r = 16;
        out->g = 8;
        out->pattern = RGB_PATTERN_UNKNOWN_AMBER;
        out->priority = 8;
    } else {
        out->priority = 8;
    }
}
