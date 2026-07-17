// PrivacySense Matter Room Hub - ui.h
//
// RGB LED (GPIO 8, on-board WS2812) and optional SSD1306 OLED (I2C bus 0).
// Task profile (task-architecture.md §4.5):
//   - Priority 3 (medium-low)
//   - Stack   3072 B
//   - Period  200 ms
//   - Watchdog feed every iteration
//
// RGB priority table: state-model.md §6. Only the highest-priority state is
// shown at any moment. Long-press countdown overrides everything.

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Lifecycle ---
esp_err_t ui_init(void);

// ui_task entry point. Created by app_main with stack 3072, prio 3.
void ui_task(void *pvParameters);

// Called by button_task during long-press to render the 5 s countdown.
// Overrides the normal RGB state. Contract:
//   0:                     clear override (LED returns to state-based rendering)
//   1..5:                  pre-threshold countdown (red blink per second)
//   UI_LONG_PRESS_COMMITTED: post-threshold committed state (solid red until
//                          release — the long-press will fire on release)
#define UI_LONG_PRESS_COMMITTED  255U
void ui_set_long_press_countdown(uint8_t remaining_seconds);

#ifdef __cplusplus
}
#endif
