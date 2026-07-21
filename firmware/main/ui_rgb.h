#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "room_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RGB_PATTERN_RED_BLINK = 0,
    RGB_PATTERN_RED_YELLOW_BLIP,
    RGB_PATTERN_BLUE_FAST_BLINK,
    RGB_PATTERN_WHITE_SLOW_BLINK,
    RGB_PATTERN_YELLOW_STEADY,
    RGB_PATTERN_WARM_WHITE,
    RGB_PATTERN_BLUE_LOW,
    RGB_PATTERN_GREEN,
    RGB_PATTERN_UNKNOWN_AMBER,
    RGB_PATTERN_OFF,
} ui_rgb_pattern_t;

typedef struct {
    uint8_t r, g, b;
    uint8_t         priority;   // 0=override, 1-8
    ui_rgb_pattern_t pattern;
} ui_rgb_output_t;

void ui_rgb_compute(const room_state_t *st, uint32_t now_ms, ui_rgb_output_t *out);

#ifdef __cplusplus
}
#endif
