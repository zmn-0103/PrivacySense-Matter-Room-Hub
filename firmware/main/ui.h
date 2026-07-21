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

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Lifecycle ---
// Initialise the RGB LED. On failure the function releases all resources
// (LED strip + internal queue) and returns a non-OK esp_err_t; the caller
// MUST treat UI as unavailable and MUST NOT spawn ui_task.
//
// Design contract (state-model.md §6, AGENTS.md §3 "本地可用性"):
//   - UI is a NON-critical peripheral. A broken on-board WS2812 or RMT
//     channel MUST NOT prevent sensors / state machine / network / Matter
//     from running. The caller is expected to skip ui_task spawn on failure
//     and continue boot.
//   - After a failed ui_init, ui_set_long_press_countdown() and ui_task()
//     are safe to call (no-op + log). This keeps button.c / main.c simple:
//     they do not need conditional guards around every UI call.
esp_err_t ui_init(void);

// Returns true iff ui_init() succeeded and the strip handle is live.
// button_task can use this to decide whether long-press countdown writes
// are useful (they are silently dropped if UI is degraded).
bool ui_is_initialized(void);

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

// ── OLED display ─────────────────────────────────────────────────────
// Called from ui_task to render the current room state on the OLED.
// No-op (returns ESP_OK) if OLED init failed or OLED is not populated.
// Returns the ssd1306_flush() result so the caller can count failures and
// back off without blocking RGB / sensors / state machine / network.
esp_err_t ui_oled_render_state(void);

#ifdef __cplusplus
}
#endif
