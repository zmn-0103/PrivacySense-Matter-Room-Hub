// PrivacySense Matter Room Hub - SSD1306 OLED driver (I2C bus 0)
//
// 128x64 monochrome OLED on I2C_NUM_0 (GPIO 6/7), 7-bit address 0x3C.
// Uses ESP-IDF hardware I2C master driver (NOT STM32 bit-bang).
//
// Lifecycle (see also ui.h):
//   ssd1306_init()      — install I2C driver + send init sequence
//   ssd1306_draw_text() — write text to the internal buffer
//   ssd1306_flush()     — flush the buffer to the display
//   ssd1306_clear()     — clear buffer + flush
//   ssd1306_deinit()    — clear display + delete I2C driver
//
// All functions are safe to call before ssd1306_init() (they no-op).
// Ownership: called from ui_task context. No ISR.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/i2c.h"
#include "pins.h"               // PIN_I2C0_PORT, PIN_I2C_ADDR_SSD1306, PIN_I2C0_FREQ_HZ

#ifdef __cplusplus
extern "C" {
#endif

// SSD1306 dimensions and buffer
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64
#define SSD1306_PAGES           (SSD1306_HEIGHT / 8)
#define SSD1306_BUFFER_SIZE     (SSD1306_WIDTH * SSD1306_PAGES)

// I2C config — aliases to the frozen pin table (pins.h).
#define SSD1306_I2C_PORT        PIN_I2C0_PORT
#define SSD1306_I2C_ADDR        PIN_I2C_ADDR_SSD1306
#define SSD1306_I2C_FREQ_HZ     PIN_I2C0_FREQ_HZ

// ── Lifecycle ────────────────────────────────────────────────────────────
// Install I2C master driver on the given SDA/SCL pins, send the SSD1306
// init sequence, and clear the display. Returns ESP_OK on success.
// On failure, no I2C resources are left allocated — caller can retry.
esp_err_t ssd1306_init(i2c_port_t port, uint8_t addr_7bit,
                       gpio_num_t sda_gpio, gpio_num_t scl_gpio);

// De-initialise: clear display, delete I2C driver. Safe to call even if
// ssd1306_init() was never called or previously failed.
void ssd1306_deinit(void);

// Returns true iff the I2C driver is installed and the OLED is initialized.
bool ssd1306_is_initialized(void);

// ── Display ──────────────────────────────────────────────────────────────
// Clear only the internal buffer (no I2C flush). Use before a sequence of
// draw operations followed by a single ssd1306_flush() to avoid ghost chars.
void ssd1306_clear_buffer(void);

// Clear the internal buffer and flush to display.
void ssd1306_clear(void);

// Flush the internal buffer to the OLED via I2C.
// Returns ESP_OK on success, or the first failing I2C error. If the column/
// page addressing command fails, NO data is written. Safe to call before
// ssd1306_init() (returns ESP_OK as a no-op).
esp_err_t ssd1306_flush(void);

// Draw a NUL-terminated string at the given page (row of 8 pixels, 0..7)
// and column (0..127). Characters beyond the display width are clipped.
// Uses a built-in 6x8 font (ASCII 0x20..0x7E).
void ssd1306_draw_text(uint8_t page, uint8_t col, const char *text);

// Draw a single character at the given page and column.
// Returns the column of the next character.
uint8_t ssd1306_draw_char(uint8_t page, uint8_t col, char ch);

#ifdef __cplusplus
}
#endif
