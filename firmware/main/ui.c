// PrivacySense Matter Room Hub - ui.c
//
// RGB LED driver (GPIO 8, on-board WS2812 via ESP-IDF led_strip) + optional
// OLED (SSD1306 on I2C bus 0). Skeleton implementation; only the RGB
// priority table wiring is in place.
//
// Priority table: state-model.md §6.
// Long-press countdown overrides all other states.

#include "ui.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led_strip.h"

#include "pins.h"
#include "room_state.h"

static const char *TAG = "ui";

#define UI_TASK_PERIOD_MS       200U
#define RGB_BRIGHTNESS_SCALE(x) ((x) * 255 / 100)   // percent → 0..255

static led_strip_handle_t s_strip = NULL;
static volatile uint8_t   s_long_press_remaining = 0;   // 0 = no override

esp_err_t ui_init(void)
{
    if (s_strip != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // led_strip RMT config for ESP32-C6 (single LED on GPIO 8).
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = PIN_RGB_LED_GPIO,
        .max_leds       = PIN_RGB_LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,   // 10 MHz
        .flags.with_dma    = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device: %s", esp_err_to_name(ret));
        return ret;
    }
    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "init ok (RGB on GPIO %d)", PIN_RGB_LED_GPIO);
    return ESP_OK;
}

void ui_set_long_press_countdown(uint8_t remaining_seconds)
{
    s_long_press_remaining = remaining_seconds;
}

static void render_rgb_for_state(const room_state_t *st)
{
    // TODO: implement full priority table from state-model.md §6.
    //       For now, render a minimal mapping so the LED visibly reflects state.
    uint8_t r = 0, g = 0, b = 0;

    if (!st->radar_online || !st->env_sensor_online) {
        // P1: sensor failure → red slow blink (1 Hz)
        // xTaskGetTickCount() returns ticks (100 Hz), not ms — convert first.
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

    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

void ui_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    room_state_t snap;
    uint32_t loop = 0;

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), uxTaskPriorityGet(NULL));

    for (;;) {
        if (s_long_press_remaining == UI_LONG_PRESS_COMMITTED) {
            // Post-threshold committed state: solid red until release.
            // The long-press event fires on release; the solid red tells the
            // user "you've held long enough, release now to confirm".
            led_strip_set_pixel(s_strip, 0, 64, 0, 0);
            led_strip_refresh(s_strip);
        } else if (s_long_press_remaining > 0) {
            // Pre-threshold countdown: 1 Hz red blink (500 ms on, 500 ms off).
            // The blink phase is derived from the current tick count, NOT from
            // the countdown value — deriving it from the value would make the
            // LED hold each state for 1 s (2 s period = 0.5 Hz) because the
            // value only changes once per second.
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            bool on = (now_ms % 1000U) < 500U;
            uint8_t r = on ? 64 : 0;
            led_strip_set_pixel(s_strip, 0, r, 0, 0);
            led_strip_refresh(s_strip);
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
