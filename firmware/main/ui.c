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
#include <string.h>

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
#include "ssd1306.h"
#include "ui_rgb.h"

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

    // 5) Optional: initialise SSD1306 OLED on I2C bus 0. Failure is not
    //    fatal — the display is a non-critical UI element. OLED init may
    //    fail because the module is not populated or I2C bus scan fails.
    //    When OLED is degraded, ssd1306_draw_text() and ssd1306_flush()
    //    are safe no-ops.
    {
        esp_err_t oled_ret = ssd1306_init(SSD1306_I2C_PORT,
                                           SSD1306_I2C_ADDR,
                                           PIN_I2C0_SDA_GPIO,
                                           PIN_I2C0_SCL_GPIO);
        if (oled_ret != ESP_OK) {
            ESP_LOGW(TAG, "ssd1306_init: %s — continuing WITHOUT OLED",
                     esp_err_to_name(oled_ret));
        } else {
            ssd1306_clear();
        }
    }

    ESP_LOGI(TAG, "init ok (RGB on GPIO %d, RMT res %u Hz, OLED=%s)",
             PIN_RGB_LED_GPIO, (unsigned)LED_RMT_RESOLUTION_HZ,
             ssd1306_is_initialized() ? "yes" : "no");
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

// Direct LED override for factory reset confirmation. Called synchronously
// from state_machine_task — the system is about to reboot, so we bypass the
// normal ui_task rendering pipeline and set the LED directly.
// If UI is degraded, this is a safe no-op (ESP_LOGW once per call).
void ui_show_factory_reset_confirm(void)
{
    if (s_strip == NULL) {
        ESP_LOGW(TAG, "factory reset confirm: LED not available (UI degraded)");
        return;
    }
    led_strip_set_pixel(s_strip, 0, 0, 32, 0);   // green, medium brightness
    led_strip_refresh(s_strip);
}

void ui_show_factory_reset_failed(void)
{
    if (s_strip == NULL) {
        ESP_LOGW(TAG, "factory reset failed: LED not available (UI degraded)");
        return;
    }
    led_strip_set_pixel(s_strip, 0, 32, 0, 0);    // red, medium brightness
    led_strip_refresh(s_strip);
}

static const char *rgb_priority_label(uint8_t prio, ui_rgb_pattern_t pattern)
{
    if (pattern == RGB_PATTERN_UNKNOWN_AMBER) return "P8-unknown";
    if (prio == 0) return "P0-override";
    if (prio == 1) return "P1-sensor-fail";
    if (prio == 2) return "P2-commissioning";
    if (prio == 3) return "P3-wifi-down";
    if (prio == 4) return "P4-env-alert";
    if (prio == 5) return "P5-occupied-night";
    if (prio == 6) return "P6-occupied-quiet";
    if (prio == 7) return "P7-occupied-normal";
    return "P8-vacant";
}

static void render_rgb_for_state(const room_state_t *st, uint32_t now_ms)
{
    ui_rgb_output_t out;
    ui_rgb_compute(st, now_ms, &out);

    (void)ui_write_rgb(out.r, out.g, out.b);

    static ui_rgb_pattern_t s_last_pattern = RGB_PATTERN_OFF;
    if (out.pattern != s_last_pattern) {
        s_last_pattern = out.pattern;
        ESP_LOGI(TAG, "RGB: %s (r=%u g=%u b=%u)",
                 rgb_priority_label(out.priority, out.pattern),
                 (unsigned)out.r, (unsigned)out.g, (unsigned)out.b);
    }
}

// ── OLED rendering ───────────────────────────────────────────────────
esp_err_t ui_oled_render_state(void)
{
    if (!ssd1306_is_initialized()) return ESP_OK;   // degraded: no display

    room_state_t snap;
    if (room_state_snapshot(&snap) != ESP_OK) return ESP_OK;

    // Clear buffer before drawing to prevent ghost characters from
    // previous frame (e.g. "ALERT" → "OK" leaving "ERT").
    ssd1306_clear_buffer();

    // Page 0: occupancy + Wi-Fi (fixed width 16 chars to erase leftovers)
    {
        char line[22];
        snprintf(line, sizeof(line), "RM: %-3s %-4s",
                 snap.occupancy == OCCUPANCY_OCCUPIED ? "OCC" :
                 snap.occupancy == OCCUPANCY_VACANT  ? "VAC" : "UNK",
                 snap.wifi_connected ? "WiFi" : "--");
        ssd1306_draw_text(0, 0, line);
    }

    // Page 1: user mode (fixed width 8)
    {
        char line[10];
        const char *mode_str = "NORMAL";
        if (snap.user_mode == MODE_NIGHT)       mode_str = "NIGHT";
        else if (snap.quiet_active)             mode_str = "QUIET";
        else if (snap.user_mode == MODE_QUIET)  mode_str = "QUIET";
        snprintf(line, sizeof(line), "%-8s", mode_str);
        ssd1306_draw_text(1, 0, line);
    }

    // Page 2: env alert state (fixed width 8)
    {
        char line[10];
        snprintf(line, sizeof(line), "%-8s",
                 snap.env_alert == ALERT_ACTIVE ? "ALERT" : "OK");
        ssd1306_draw_text(2, 0, line);
    }

    // Page 3: sensor online (fixed width 16)
    {
        char line[18];
        snprintf(line, sizeof(line), "R:%-3s E:%-3s",
                 snap.radar_online      ? "ON"  : "OFF",
                 snap.env_sensor_online  ? "ON"  : "OFF");
        ssd1306_draw_text(3, 0, line);
    }

    // Propagate the real flush result so the task can count failures.
    return ssd1306_flush();
}

// OLED refresh is decoupled from the 200 ms RGB tick: it runs at ~1 Hz and
// backs off on failure so a broken/removed OLED cannot spin the I2C bus or
// flood the log (Reviewer P1). RGB stays at 200 ms regardless.
#define UI_OLED_NORMAL_MS     1000U
#define UI_OLED_BACKOFF_MS    5000U
#define UI_OLED_BACKOFF_FAILS 3u

static uint32_t s_oled_last_ms      = 0;
static uint32_t s_oled_interval_ms  = UI_OLED_NORMAL_MS;
static uint32_t s_oled_fail_count   = 0;

static bool ui_oled_warn_allowed(void)
{
    static uint32_t s_last_ms = 0;
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t elapsed = now - s_last_ms;
    if (elapsed >= 5000U) { s_last_ms = now; return true; }
    return false;
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
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

#ifdef CONFIG_UI_RGB_DIAGNOSTIC
        // Diagnostic mode: cycle through 6 colours + off, 2 s each, repeated.
        // Overrides ALL normal rendering (both long-press and state-based).
        // OLED refresh and heartbeat still run below.
        {
            uint8_t cycle = (uint8_t)((now_ms / 2000) % 7);
            static uint8_t s_last_cycle = 0xFF;
            if (cycle != s_last_cycle) {
                s_last_cycle = cycle;
                static const char *diag_labels[] = {
                    "red", "green", "blue", "yellow", "white",
                    "warm-white", "off"
                };
                ESP_LOGI(TAG, "DIAGNOSTIC: %s", diag_labels[cycle]);
            }
            switch (cycle) {
            case 0: (void)ui_write_rgb(64,  0,  0); break; // red
            case 1: (void)ui_write_rgb( 0, 64,  0); break; // green
            case 2: (void)ui_write_rgb( 0,  0, 64); break; // blue
            case 3: (void)ui_write_rgb(64, 64,  0); break; // yellow
            case 4: (void)ui_write_rgb(32, 32, 32); break; // white
            case 5: (void)ui_write_rgb(25, 18,  5); break; // warm white
            case 6: (void)ui_write_rgb( 0,  0,  0); break; // off (1 slot only)
            }
        }
#else
        // Peek (non-blocking) the latest long-press value written by
        // button_task. xQueuePeek does NOT remove the item.
        uint8_t long_press_remaining = 0;
        xQueuePeek(s_long_press_queue, &long_press_remaining, 0);

        if (long_press_remaining == UI_LONG_PRESS_COMMITTED) {
            // Post-threshold committed state: solid red until release.
            (void)ui_write_rgb(64, 0, 0);
        } else if (long_press_remaining > 0) {
            // Pre-threshold countdown: 1 Hz red blink (500 ms on, 500 ms off).
            bool on = (now_ms % 1000U) < 500U;
            (void)ui_write_rgb(on ? 64 : 0, 0, 0);
        } else {
            // RGB reflects state every 200 ms regardless of OLED health.
            if (room_state_snapshot(&snap) == ESP_OK) {
                render_rgb_for_state(&snap, now_ms);
            }
        }
#endif

        // OLED refresh is decoupled (~1 Hz, backed off on failure) so a
        // broken/removed display never blocks RGB / sensors / state machine
        // / network (Reviewer P1). ui_oled_render_state() is a no-op when the
        // OLED is not populated.
        if ((now_ms - s_oled_last_ms) >= s_oled_interval_ms) {
            s_oled_last_ms = now_ms;
            esp_err_t oret = ui_oled_render_state();
            if (oret != ESP_OK) {
                s_oled_fail_count++;
                if (s_oled_fail_count >= UI_OLED_BACKOFF_FAILS) {
                    s_oled_interval_ms = UI_OLED_BACKOFF_MS;
                    if (ui_oled_warn_allowed()) {
                        ESP_LOGW(TAG, "OLED flush failed x%u, backing off to %ums",
                                 (unsigned)s_oled_fail_count,
                                 (unsigned)UI_OLED_BACKOFF_MS);
                    }
                }
            } else {
                s_oled_fail_count = 0;
                s_oled_interval_ms = UI_OLED_NORMAL_MS;
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
