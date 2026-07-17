// PrivacySense Matter Room Hub - button.h
//
// GPIO9 (DevKitC-1 Boot button, active low) → ISR → gpio_evt_queue →
// button_task (debounce) → app_event_queue (consumed by state_machine_task).
//
// Task profile (task-architecture.md §4.4, §7.2):
//   - Priority 5 (medium-high)
//   - Stack   2048 B
//   - Trigger event-driven (gpio_evt_queue blocking read, 2 s timeout for
//     TWDT feed — task-architecture.md §7.2)
//
// ISR rule (task-architecture.md §5.4): clear flag → xQueueSendFromISR →
// portYIELD_FROM_ISR. NO debounce, NO I2C/UART, NO malloc, NO printf, NO mutex.

#pragma once

#include "esp_err.h"
#include "state_machine.h"   // button_event_t

#ifdef __cplusplus
extern "C" {
#endif

// --- Timing (state-model.md §3.6) ---
#define BUTTON_DEBOUNCE_MS       50U
#define BUTTON_SHORT_PRESS_MIN_MS 50U
#define BUTTON_SHORT_PRESS_MAX_MS 1000U
#define BUTTON_LONG_PRESS_MS     5000U

// --- Lifecycle ---
esp_err_t button_init(void);

// button_task entry point. Created by app_main with stack 2048, prio 5.
void button_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
