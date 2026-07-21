// PrivacySense Matter Room Hub - button.c
//
// GPIO 9 (active low) → ISR → gpio_evt_queue → button_task (debounce) →
// app_event_queue (consumed by state_machine_task).
//
// Event flow (task-architecture.md §3, §5.1):
//   ISR        → gpio_evt_queue (uint32_t gpio_num)  — raw edge interrupt
//   button_task → app_event_t (type=EVENT_BUTTON)    — debounced press/release
//
// ISR contract (task-architecture.md §5.4):
//   - Clear interrupt flag
//   - xQueueSendFromISR(gpio_evt_queue, gpio_num, ...)
//   - portYIELD_FROM_ISR if woken
//   - NO debounce, NO logging, NO malloc, NO mutex
//   - xQueueSendFromISR result is checked; drops are counted (not silently lost)
//
// TWDT (task-architecture.md §7.2):
//   - button_task blocks on gpio_evt_queue with a 2 s timeout, so the max
//     no-feed interval is 2 s (well below the 10 s TWDT timeout). When the
//     button is held down, the block shortens to 100 ms so the long-press
//     countdown UI can update per second.

#include "button.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pins.h"
#include "state_machine.h"   // g_app_event_queue, app_event_t, button_event_t
#include "ui.h"               // ui_set_long_press_countdown

static const char *TAG = "button";

#define GPIO_EVT_QUEUE_DEPTH    4
#define BUTTON_TASK_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)
#define BUTTON_QUEUE_SEND_MS    20U     // bounded wait per task-architecture.md §5.3
// LONG_PRESS is a critical control event (factory reset / re-commissioning,
// commissioning-lifecycle.md §4). It MUST NOT be lost to transient queue
// pressure — the user has already held the button for 5 s and cannot repeat
// the action without another 5 s wait. Use a 1 s send timeout so the
// state_machine_task (1 s drain cycle) has ample time to free a slot. A
// SHORT_PRESS remains 20 ms: it is non-critical and the user can retry easily.
#define BUTTON_LONG_PRESS_SEND_MS  1000U
// Polling interval for "is the button still held?" — used to drive the
// long-press countdown UI while the button is held down, before the release
// edge arrives. 100 ms gives a 10 Hz resolution, plenty for a per-second UI.
#define BUTTON_HOLD_POLL_MS     100U

static QueueHandle_t s_gpio_evt_queue = NULL;

// ISR drop counter (monotonic). Read by diagnostics; cleared only by factory reset.
// volatile because it's incremented in ISR and read in task context.
static volatile uint32_t s_isr_drops = 0;

// --- ISR: short, defers everything to the task ---
// Per task-architecture.md §5.4: NO debounce here. The 50 ms software debounce
// is done in button_task after the queue receive, using the GPIO level read
// at the time of the edge plus a re-read after BUTTON_DEBOUNCE_MS.
static void IRAM_ATTR button_isr_handler(void *arg)
{
    (void)arg;
    uint32_t gpio_num = (uint32_t)PIN_BUTTON_GPIO;
    BaseType_t higher_priority_task_woken = pdFALSE;

    // xQueueSendFromISR result MUST be checked: a dropped release edge would
    // leave `pressed=true` forever, locking out all subsequent button events.
    if (xQueueSendFromISR(s_gpio_evt_queue, &gpio_num,
                          &higher_priority_task_woken) != pdTRUE) {
        // Queue full — count the drop. The task will re-sync GPIO state on
        // its next timeout, so a transient overflow cannot wedge the button.
        s_isr_drops++;
        return;
    }
    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t button_init(void)
{
    if (s_gpio_evt_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // GPIO config: input + internal pull-up + both-edge interrupt.
    // Both edges are required: falling edge marks press-start, rising edge
    // marks release; without the rising edge the task can never compute
    // press duration and would never emit SHORT/LONG events.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = PIN_BUTTON_PULL,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_gpio_evt_queue = xQueueCreate(GPIO_EVT_QUEUE_DEPTH, sizeof(uint32_t));
    if (s_gpio_evt_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "isr_service install: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = gpio_isr_handler_add(PIN_BUTTON_GPIO, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "isr_handler_add: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "init ok (GPIO %d, active low, both-edge IRQ)",
             PIN_BUTTON_GPIO);
    return ESP_OK;
}

// Read GPIO level and confirm it has been stable for BUTTON_DEBOUNCE_MS.
// Returns the stable level (0 = pressed/active-low, 1 = released), or -1 if
// the level changed during the debounce window (treat as spurious).
static int read_debounced_level(void)
{
    int level1 = gpio_get_level(PIN_BUTTON_GPIO);
    vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
    int level2 = gpio_get_level(PIN_BUTTON_GPIO);
    return (level1 == level2) ? level1 : -1;
}

// Classify a release by duration and forward the corresponding app_event_t to
// g_app_event_queue. `source` is included in the log for diagnostics so
// recovered releases (from the poll branch) are distinguishable from normal
// edge-driven releases. Out-of-range durations are logged and dropped.
//
// Send timeout is kind-dependent: LONG_PRESS gets a 1 s window (critical
// control event — factory reset must survive transient queue pressure),
// SHORT_PRESS gets 20 ms (non-critical, user can retry). A dropped
// LONG_PRESS is logged at ERROR with a distinctive tag so diagnostics can
// detect "factory reset was lost".
static void classify_and_emit_release(uint32_t press_start_ms, uint32_t now_ms,
                                      const char *source)
{
    uint32_t dur = now_ms - press_start_ms;
    button_event_t kind;
    if (dur >= BUTTON_LONG_PRESS_MS) {
        kind = BUTTON_EVENT_LONG_PRESS;
        ESP_LOGI(TAG, "LONG press (%u ms, %s)", (unsigned)dur, source);
    } else if (dur >= BUTTON_SHORT_PRESS_MIN_MS &&
               dur <= BUTTON_SHORT_PRESS_MAX_MS) {
        kind = BUTTON_EVENT_SHORT_PRESS;
        ESP_LOGI(TAG, "SHORT press (%u ms, %s)", (unsigned)dur, source);
    } else {
        ESP_LOGD(TAG, "ignored press (%u ms, %s)", (unsigned)dur, source);
        return;
    }
    app_event_t ev = {
        .type          = EVENT_BUTTON,
        .data.button   = kind,
        .timestamp_ms  = now_ms,
    };
    TickType_t send_wait = (kind == BUTTON_EVENT_LONG_PRESS)
        ? pdMS_TO_TICKS(BUTTON_LONG_PRESS_SEND_MS)
        : pdMS_TO_TICKS(BUTTON_QUEUE_SEND_MS);
    if (xQueueSend(g_app_event_queue, &ev, send_wait) != pdTRUE) {
        if (kind == BUTTON_EVENT_LONG_PRESS) {
            // Critical control event lost. The factory reset / re-commission
            // intent did NOT reach state_machine_task. Log at ERROR with a
            // distinctive tag so diagnostics / monitoring can catch this.
            // TODO: surface via a dedicated control queue or sticky flag so
            //       state_machine_task can recover it on its next drain
            //       (task-architecture.md §5.3).
            ESP_LOGE(TAG, "LONG_PRESS DROPPED (%s) — factory reset intent "
                     "lost; app_event_queue full after %u ms",
                     source, BUTTON_LONG_PRESS_SEND_MS);
        } else {
            ESP_LOGW(TAG, "app_event_queue full; SHORT press dropped (%s)",
                     source);
        }
    }
}

void button_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint32_t gpio_num;
    uint32_t press_start_ms = 0;
    bool     pressed = false;
    uint8_t  last_countdown_shown = 0;
    uint32_t loop = 0;

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    for (;;) {
        // Use a shorter block when the button is held down so we can poll the
        // hold duration and drive the long-press countdown UI. When idle,
        // block up to BUTTON_TASK_TIMEOUT_MS to feed TWDT.
        TickType_t block_ticks = pressed
            ? pdMS_TO_TICKS(BUTTON_HOLD_POLL_MS)
            : pdMS_TO_TICKS(BUTTON_TASK_TIMEOUT_MS);

        if (xQueueReceive(s_gpio_evt_queue, &gpio_num, block_ticks) == pdTRUE) {
            // Edge event arrived. Debounce: confirm the level is stable for
            // BUTTON_DEBOUNCE_MS before accepting the transition.
            int level = read_debounced_level();
            if (level < 0) {
                ESP_LOGD(TAG, "spurious edge (level unstable during %u ms)",
                         BUTTON_DEBOUNCE_MS);
                goto feed_wdt;
            }

            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

            if (level == 0 && !pressed) {
                // Falling edge → press start.
                press_start_ms = now_ms;
                pressed = true;
                last_countdown_shown = 0;
            } else if (level == 1 && pressed) {
                // Rising edge → release; classify and emit.
                pressed = false;
                ui_set_long_press_countdown(0);   // clear countdown overlay
                classify_and_emit_release(press_start_ms, now_ms, "edge");
            }
            // level == 0 && pressed  → duplicate falling edge, ignore.
            // level == 1 && !pressed → duplicate rising edge, ignore.
        } else if (pressed) {
            // No edge event within BUTTON_HOLD_POLL_MS, but button is still
            // held down. Two responsibilities:
            //   (1) Recover from a dropped release edge: if the ISR queue
            //       overflowed and lost the rising edge, the task would
            //       otherwise keep `pressed=true` forever, locking out all
            //       subsequent button events and leaving the UI override on.
            //       Read GPIO; if it reads released, debounce-confirm and
            //       classify the release from this poll branch.
            //   (2) Drive the long-press countdown UI (state-model.md §3.6):
            //       during 0..5 s show 5→1 (red blink), at ≥ 5 s show
            //       UI_LONG_PRESS_COMMITTED (solid red — "release to confirm").
            if (gpio_get_level(PIN_BUTTON_GPIO) == 1) {
                // GPIO reads released — debounce to be sure it's not noise.
                int level = read_debounced_level();
                if (level == 1) {
                    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    pressed = false;
                    ui_set_long_press_countdown(0);
                    classify_and_emit_release(press_start_ms, now_ms,
                                              "poll-recovery");
                    goto feed_wdt;
                }
                // Spurious (level unstable during debounce) — fall through to
                // countdown update; the next poll will retry the recovery.
            }

            uint32_t held_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS)
                               - press_start_ms;
            uint8_t countdown;
            if (held_ms >= BUTTON_LONG_PRESS_MS) {
                // Threshold reached: switch to committed state (solid red).
                // The LONG_PRESS event fires on release, not at threshold, so
                // the user has clear feedback "you've held long enough".
                countdown = UI_LONG_PRESS_COMMITTED;
            } else {
                // Pre-threshold: ceil((5000 - held_ms) / 1000) → 5, 4, 3, 2, 1.
                // At held_ms=0     → remaining_ms=5000 → ceil(5000/1000)=5
                // At held_ms=999   → remaining_ms=4001 → ceil(4001/1000)=5
                // At held_ms=1000  → remaining_ms=4000 → ceil(4000/1000)=4
                // At held_ms=4999  → remaining_ms=1    → ceil(1/1000)=1
                // At held_ms=5000  → handled by the committed branch above.
                uint32_t remaining_ms = BUTTON_LONG_PRESS_MS - held_ms;
                countdown = (uint8_t)((remaining_ms + 999U) / 1000U);
                if (countdown > 5U) {
                    countdown = 5U;
                }
            }
            if (countdown != last_countdown_shown) {
                ui_set_long_press_countdown(countdown);
                last_countdown_shown = countdown;
            }
        }
feed_wdt:
        // Feed TWDT regardless of whether an event arrived. Max gap = 2 s
        // (BUTTON_TASK_TIMEOUT_MS) per task-architecture.md §7.2. When the
        // button is held, the poll interval is 100 ms — still well under 2 s.
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop % 50) == 0) {
            ESP_LOGI(TAG, "heartbeat: loop=%u, stack_hwm=%u bytes, isr_drops=%lu",
                     (unsigned)loop,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL),
                     (unsigned long)s_isr_drops);
        }
    }
}
