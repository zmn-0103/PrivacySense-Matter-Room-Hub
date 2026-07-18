// PrivacySense Matter Room Hub - ui.c
//
// RGB LED driver (GPIO 8, on-board WS2812 via led_strip 2.x API) + optional
// OLED (SSD1306 on I2C bus 0). Skeleton implementation; only the RGB
// priority table wiring is in place.
//
// Priority table: state-model.md §6.
// Long-press countdown overrides all other states.
//
// LED ownership: ui is the ONLY initialiser of the on-board WS2812. The
// esp-matter device HAL (esp32c6_devkit_c) also references an LED, but for v1
// we do NOT let it initialise the strip — the device HAL's led_init is
// bypassed by NOT including the device HAL component dirs (see
// firmware/CMakeLists.txt). If this changes, the strip init MUST be moved to
// a single owner to avoid two callers racing for the same RMT channel.
//
// RMT driver generation: led_strip 2.x uses the new driver/rmt_tx.h
// internally. We must NOT include driver/rmt.h (legacy) anywhere in the
// project, because env_sensor uses driver/rmt_rx.h (new). Mixing legacy +
// new RMT drivers triggers an early abort() before app_main():
//   "CONFLICT! driver_ng is not allowed to be used with the legacy driver"
//
// Cross-task state sharing (AGENTS.md §4: "禁止无保护访问共享状态"):
//   button_task → ui_task long-press countdown is delivered via a 1-element
//   queue (xQueueOverwrite + xQueuePeek), NOT via a `volatile` variable.
//   `volatile` does not guarantee atomicity or visibility on multi-core /
//   pipelined targets and does not satisfy the project's shared-state
//   protection rule. The queue provides single-producer / single-consumer
//   "latest value wins" semantics with proper memory barriers.
//
// Degraded mode (state-model.md §6, AGENTS.md §3 "本地可用性"):
//   UI is a NON-critical peripheral. If ui_init() fails, all resources are
//   released and ui_is_initialized() returns false. Callers (main.c,
//   button.c) MUST skip ui_task spawn and continue boot — sensors / state
//   machine / network / Matter run without UI. All UI public APIs are safe
//   to call after a failed init: they no-op + log at DEBUG so callers do not
//   need conditional guards everywhere.

#include "ui.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// led_strip 2.x: opaque-handle API. The driver creates and owns the RMT TX
// channel internally — the caller does NOT invoke rmt_config() /
// rmt_driver_install() (those are legacy driver/rmt.h APIs).
#include "led_strip.h"

#include "pins.h"
#include "room_state.h"

static const char *TAG = "ui";

#define UI_TASK_PERIOD_MS       200U
#define RGB_BRIGHTNESS_SCALE(x) ((x) * 255 / 100)   // percent → 0..255

// led_strip 2.x RMT tick resolution. 10 MHz → 100 ns granularity, sufficient
// for WS2812 bit-cell timing (T0H ≈ 400 ns, T1H ≈ 800 ns, RST ≥ 50 µs).
// NOTE: this is the RMT tick frequency, NOT the WS2812 bit rate. The WS2812
// nominal data rate is ~800 kbit/s (1.25 µs per bit), derived from the bit
// cells encoded into the RMT symbols — not from this clock directly.
#define LED_RMT_RESOLUTION_HZ   (10 * 1000 * 1000)

// Rate-limit for runtime LED write errors. The task ticks every 200 ms; if
// the strip is broken we would otherwise flood the console. One log per ~5 s
// is enough for monitoring.
#define UI_WRITE_ERR_LOG_INTERVAL_MS  5000U

static led_strip_handle_t s_strip             = NULL;
// 1-element queue: button_task writes via xQueueOverwrite, ui_task reads via
// xQueuePeek (non-blocking). Created in ui_init() ONLY after the strip is
// successfully initialised, so a failed ui_init leaves NO resources behind.
static QueueHandle_t      s_long_press_queue  = NULL;
static uint32_t           s_last_err_log_ms   = 0;
static bool               s_err_logged_once   = false;

// Write RGB to the strip with both set_pixel + refresh checked. Returns
// ESP_OK only if both steps succeed; logs a rate-limited warning on failure.
// Always returns the underlying esp_err_t so callers can react if needed
// (current callers ignore it — degrading one LED frame is acceptable).
//
// Rate-limiting notes:
//   - First-ever failure is logged immediately (s_err_logged_once gate).
//   - Subsequent failures are deduplicated by UI_WRITE_ERR_LOG_INTERVAL_MS.
//   - Tick wrap-around is handled by unsigned subtraction: when now_ms wraps
//     below s_last_err_log_ms, `(now_ms - s_last_err_log_ms)` yields the
//     correct small positive elapsed value modulo 2^32 (e.g.
//     0x00000002 - 0xFFFFFFFE == 4). This is the FreeRTOS-standard tick
//     wrap idiom — do NOT replace with a signed/conditional comparison.
static esp_err_t ui_write_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip == NULL) {
        return ESP_ERR_INVALID_STATE;   // degraded mode — silent (caller logs)
    }
    esp_err_t ret = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (ret != ESP_OK) {
        goto log_err;
    }
    ret = led_strip_refresh(s_strip);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

log_err:
    {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = now_ms - s_last_err_log_ms;   // unsigned: wraps correctly
        if (!s_err_logged_once || elapsed >= UI_WRITE_ERR_LOG_INTERVAL_MS) {
            ESP_LOGW(TAG, "led write failed: %s (r=%u g=%u b=%u)",
                     esp_err_to_name(ret),
                     (unsigned)r, (unsigned)g, (unsigned)b);
            s_last_err_log_ms = now_ms;
            s_err_logged_once = true;
        }
    }
    return ret;
}

esp_err_t ui_init(void)
{
    if (s_strip != NULL || s_long_press_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1) Strip configuration: GPIO, LED count, pixel format, LED model.
    //    led_strip 2.x derives WS2812 timing (T0H/T1H/RST) from led_model
    //    + the RMT resolution configured below.
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = PIN_RGB_LED_GPIO,
        .max_leds         = PIN_RGB_LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // 2) RMT backend configuration. 10 MHz is the standard for WS2812 in
    //    ESP-IDF examples; with_dma=false is correct for a single LED on
    //    ESP32-C6 (the RMT channel has ample capacity for 1 pixel).
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src          = RMT_CLK_SRC_DEFAULT,
        .resolution_hz    = LED_RMT_RESOLUTION_HZ,
        .flags.with_dma   = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device: %s", esp_err_to_name(ret));
        // Nothing else has been allocated yet — nothing to roll back.
        s_strip = NULL;
        return ret;
    }

    // 3) Turn off the LED on init. A failure here means the strip was
    //    created but cannot accept commands — treat as init FAILURE and
    //    release the strip. We do NOT report success with a broken handle,
    //    because the 200 ms task would then spam rate-limited errors
    //    forever and the user would have no indication that init was bad.
    //    (Strategy chosen per Reviewer M2: "init failure + release strip".)
    //
    //    The original clear_ret is propagated to the caller so the caller
    //    can distinguish (e.g.) an I/O timeout from a generic ESP_FAIL.
    //    led_strip_del() is also checked: a double failure is logged but
    //    does not mask the original clear_ret.
    esp_err_t clear_ret = led_strip_clear(s_strip);
    if (clear_ret != ESP_OK) {
        ESP_LOGE(TAG, "initial clear failed: %s — releasing strip",
                 esp_err_to_name(clear_ret));
        esp_err_t del_ret = led_strip_del(s_strip);
        if (del_ret != ESP_OK) {
            ESP_LOGE(TAG, "led_strip_del also failed: %s (handle leaked)",
                     esp_err_to_name(del_ret));
        }
        s_strip = NULL;
        return clear_ret;
    }

    // 4) Create the cross-task queue ONLY after the strip is confirmed good.
    //    Previous version created the queue first and leaked it on strip
    //    failure; this version guarantees ui_init() leaves nothing behind
    //    on any failure path, so a retry is safe.
    s_long_press_queue = xQueueCreate(1, sizeof(uint8_t));
    if (s_long_press_queue == NULL) {
        ESP_LOGE(TAG, "xQueueCreate(long_press) failed — releasing strip");
        esp_err_t del_ret = led_strip_del(s_strip);
        if (del_ret != ESP_OK) {
            ESP_LOGE(TAG, "led_strip_del also failed: %s (handle leaked)",
                     esp_err_to_name(del_ret));
        }
        s_strip = NULL;
        return ESP_ERR_NO_MEM;
    }
    uint8_t initial = 0;   // no override
    xQueueOverwrite(s_long_press_queue, &initial);

    ESP_LOGI(TAG, "init ok (RGB on GPIO %d, RMT res %u Hz)",
             PIN_RGB_LED_GPIO, (unsigned)LED_RMT_RESOLUTION_HZ);
    return ESP_OK;
}

bool ui_is_initialized(void)
{
    return s_strip != NULL;
}

void ui_set_long_press_countdown(uint8_t remaining_seconds)
{
    // Called from button_task. Cross-task delivery uses xQueueOverwrite
    // (length-1 queue → "latest value wins"), NOT a volatile variable.
    // xQueueOverwrite is safe from a task context and provides the necessary
    // memory barriers.
    //
    // Degraded mode: if ui_init() failed, the queue is NULL. We log at DEBUG
    // (not WARNING — would flood the button task at 5 Hz during long press)
    // and return. button.c does not need to check ui_is_initialized() before
    // every call; this keeps the call site simple.
    if (s_long_press_queue == NULL) {
        ESP_LOGD(TAG, "ui_set_long_press_countdown dropped (UI degraded, value=%u)",
                 (unsigned)remaining_seconds);
        return;
    }
    xQueueOverwrite(s_long_press_queue, &remaining_seconds);
}

static void render_rgb_for_state(const room_state_t *st)
{
    // TODO: implement full priority table from state-model.md §6.
    //       For now, render a minimal mapping so the LED visibly reflects state.
    uint8_t r = 0, g = 0, b = 0;

    if (!st->radar_online || !st->env_sensor_online) {
        // P1: sensor failure → red slow blink (1 Hz)
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        r = (now_ms % 1000 < 500) ? 32 : 0;
    } else if (!st->wifi_connected) {
        // P3: Wi-Fi down → white slow blink (0.5 Hz)
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        r = g = b = (now_ms % 2000 < 1000) ? 16 : 0;
    } else if (st->occupancy == OCCUPANCY_OCCUPIED) {
        // P7: occupied + normal → green
        g = 64;
    } else {
        // P8: vacant → off
        r = g = b = 0;
    }

    (void)ui_write_rgb(r, g, b);
}

void ui_task(void *pvParameters)
{
    (void)pvParameters;

    // Defensive: if ui_task is somehow spawned after a failed ui_init, exit
    // immediately. main.c is expected NOT to spawn this task when
    // ui_is_initialized() is false, but this guard protects against
    // future callers.
    if (!ui_is_initialized()) {
        ESP_LOGE(TAG, "ui_task spawned with no strip — exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    room_state_t snap;
    uint32_t loop = 0;

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    for (;;) {
        // Peek (non-blocking) the latest long-press value written by
        // button_task. xQueuePeek does NOT remove the item, so the value
        // persists until button_task overwrites it with the next state.
        uint8_t long_press_remaining = 0;
        xQueuePeek(s_long_press_queue, &long_press_remaining, 0);

        if (long_press_remaining == UI_LONG_PRESS_COMMITTED) {
            // Post-threshold committed state: solid red until release.
            (void)ui_write_rgb(64, 0, 0);
        } else if (long_press_remaining > 0) {
            // Pre-threshold countdown: 1 Hz red blink (500 ms on, 500 ms off).
            // Blink phase derived from tick count, NOT from countdown value
            // (deriving from value would give 2 s period = 0.5 Hz).
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            bool on = (now_ms % 1000U) < 500U;
            uint8_t r = on ? 64 : 0;
            (void)ui_write_rgb(r, 0, 0);
        } else {
            if (room_state_snapshot(&snap) == ESP_OK) {
                render_rgb_for_state(&snap);
            }
        }

        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop % 50) == 0) {   // ~ every 10 s
            ESP_LOGI(TAG, "heartbeat: loop=%u, stack_hwm=%u bytes",
                     (unsigned)loop,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(UI_TASK_PERIOD_MS));
    }
}
