// PrivacySense Matter Room Hub - SSD1306 OLED driver (I2C bus 0)
//
// Implemented per the SSD1306 datasheet. Uses ESP-IDF hardware I2C master
// driver (new API, not STM32 bit-bang). 128x64, page addressing mode.
//
// Font: 6x8 bitmap (ASCII 0x20..0x7E), one byte per column, 5 columns per
// character + 1 column gap. Sourced from the de facto public-domain 5x7
// font (also used by Adafruit, U8G2, etc.) packed as 6x8.

#include "ssd1306.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char *TAG = "ssd1306";

// ── 6x8 Font (ASCII 0x20..0x7E) ─────────────────────────────────────────
// Each character is 6 columns: 5 font columns + 1 blank gap column.
// Each column is 1 byte (8 pixels, MSB = top).
static const uint8_t s_font_6x8[] = {
    0x00,0x00,0x00,0x00,0x00,0x00, // SPACE
    0x00,0x00,0x5F,0x00,0x00,0x00, // !
    0x00,0x07,0x00,0x07,0x00,0x00, // "
    0x14,0x7F,0x14,0x7F,0x14,0x00, // #
    0x24,0x2A,0x7F,0x2A,0x12,0x00, // $
    0x23,0x13,0x08,0x64,0x62,0x00, // %
    0x36,0x49,0x55,0x22,0x50,0x00, // &
    0x00,0x05,0x03,0x00,0x00,0x00, // '
    0x00,0x1C,0x22,0x41,0x00,0x00, // (
    0x00,0x41,0x22,0x1C,0x00,0x00, // )
    0x08,0x2A,0x1C,0x2A,0x08,0x00, // *
    0x08,0x08,0x3E,0x08,0x08,0x00, // +
    0x00,0x50,0x30,0x00,0x00,0x00, // ,
    0x08,0x08,0x08,0x08,0x08,0x00, // -
    0x00,0x60,0x60,0x00,0x00,0x00, // .
    0x20,0x10,0x08,0x04,0x02,0x00, // /
    0x3E,0x51,0x49,0x45,0x3E,0x00, // 0
    0x00,0x42,0x7F,0x40,0x00,0x00, // 1
    0x42,0x61,0x51,0x49,0x46,0x00, // 2
    0x21,0x41,0x45,0x4B,0x31,0x00, // 3
    0x18,0x14,0x12,0x7F,0x10,0x00, // 4
    0x27,0x45,0x45,0x45,0x39,0x00, // 5
    0x3C,0x4A,0x49,0x49,0x30,0x00, // 6
    0x01,0x71,0x09,0x05,0x03,0x00, // 7
    0x36,0x49,0x49,0x49,0x36,0x00, // 8
    0x06,0x49,0x49,0x29,0x1E,0x00, // 9
    0x00,0x36,0x36,0x00,0x00,0x00, // :
    0x00,0x56,0x36,0x00,0x00,0x00, // ;
    0x00,0x08,0x14,0x22,0x41,0x00, // <
    0x14,0x14,0x14,0x14,0x14,0x00, // =
    0x41,0x22,0x14,0x08,0x00,0x00, // >
    0x02,0x01,0x51,0x09,0x06,0x00, // ?
    0x32,0x49,0x79,0x41,0x3E,0x00, // @
    0x7E,0x11,0x11,0x11,0x7E,0x00, // A
    0x7F,0x49,0x49,0x49,0x36,0x00, // B
    0x3E,0x41,0x41,0x41,0x22,0x00, // C
    0x7F,0x41,0x41,0x22,0x1C,0x00, // D
    0x7F,0x49,0x49,0x49,0x41,0x00, // E
    0x7F,0x09,0x09,0x01,0x01,0x00, // F
    0x3E,0x41,0x41,0x51,0x32,0x00, // G
    0x7F,0x08,0x08,0x08,0x7F,0x00, // H
    0x00,0x41,0x7F,0x41,0x00,0x00, // I
    0x20,0x40,0x41,0x3F,0x01,0x00, // J
    0x7F,0x08,0x14,0x22,0x41,0x00, // K
    0x7F,0x40,0x40,0x40,0x40,0x00, // L
    0x7F,0x02,0x04,0x02,0x7F,0x00, // M
    0x7F,0x04,0x08,0x10,0x7F,0x00, // N
    0x3E,0x41,0x41,0x41,0x3E,0x00, // O
    0x7F,0x09,0x09,0x09,0x06,0x00, // P
    0x3E,0x41,0x51,0x21,0x5E,0x00, // Q
    0x7F,0x09,0x19,0x29,0x46,0x00, // R
    0x46,0x49,0x49,0x49,0x31,0x00, // S
    0x01,0x01,0x7F,0x01,0x01,0x00, // T
    0x3F,0x40,0x40,0x40,0x3F,0x00, // U
    0x1F,0x20,0x40,0x20,0x1F,0x00, // V
    0x7F,0x20,0x18,0x20,0x7F,0x00, // W
    0x63,0x14,0x08,0x14,0x63,0x00, // X
    0x03,0x04,0x78,0x04,0x03,0x00, // Y
    0x61,0x51,0x49,0x45,0x43,0x00, // Z
    0x00,0x00,0x7F,0x41,0x41,0x00, // [
    0x02,0x04,0x08,0x10,0x20,0x00, // backslash
    0x41,0x41,0x7F,0x00,0x00,0x00, // ]
    0x04,0x02,0x01,0x02,0x04,0x00, // ^
    0x40,0x40,0x40,0x40,0x40,0x00, // _
    0x00,0x01,0x02,0x04,0x00,0x00, // `
    0x20,0x54,0x54,0x54,0x78,0x00, // a
    0x7F,0x48,0x44,0x44,0x38,0x00, // b
    0x38,0x44,0x44,0x44,0x20,0x00, // c
    0x38,0x44,0x44,0x48,0x7F,0x00, // d
    0x38,0x54,0x54,0x54,0x18,0x00, // e
    0x08,0x7E,0x09,0x01,0x02,0x00, // f
    0x08,0x14,0x54,0x54,0x3C,0x00, // g
    0x7F,0x08,0x04,0x04,0x78,0x00, // h
    0x00,0x44,0x7D,0x40,0x00,0x00, // i
    0x20,0x40,0x44,0x3D,0x00,0x00, // j
    0x00,0x7F,0x10,0x28,0x44,0x00, // k
    0x00,0x41,0x7F,0x40,0x00,0x00, // l
    0x7C,0x04,0x18,0x04,0x78,0x00, // m
    0x7C,0x08,0x04,0x04,0x78,0x00, // n
    0x38,0x44,0x44,0x44,0x38,0x00, // o
    0x7C,0x14,0x14,0x14,0x08,0x00, // p
    0x08,0x14,0x14,0x18,0x7C,0x00, // q
    0x7C,0x08,0x04,0x04,0x08,0x00, // r
    0x48,0x54,0x54,0x54,0x20,0x00, // s
    0x04,0x3F,0x44,0x40,0x20,0x00, // t
    0x3C,0x40,0x40,0x20,0x7C,0x00, // u
    0x1C,0x20,0x40,0x20,0x1C,0x00, // v
    0x3C,0x40,0x30,0x40,0x3C,0x00, // w
    0x44,0x28,0x10,0x28,0x44,0x00, // x
    0x0C,0x50,0x50,0x50,0x3C,0x00, // y
    0x44,0x64,0x54,0x4C,0x44,0x00, // z
    0x00,0x08,0x36,0x41,0x00,0x00, // {
    0x00,0x00,0x7F,0x00,0x00,0x00, // |
    0x00,0x41,0x36,0x08,0x00,0x00, // }
    0x08,0x08,0x2A,0x1C,0x08,0x00, // ~
};

// ── Sate ────────────────────────────────────────────────────────────
static i2c_port_t   s_port       = -1;
static uint8_t      s_addr       = 0;
static bool         s_initialized = false;

// Display buffer: 128 x 64 / 8 = 1024 bytes. Page-mode layout.
static uint8_t      s_buffer[SSD1306_BUFFER_SIZE];

// I2C helpers with null-link check.
static esp_err_t write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    if (link == NULL) { ESP_LOGE(TAG, "i2c_cmd_link_create OOM"); return ESP_ERR_NO_MEM; }
    i2c_master_start(link);
    i2c_master_write_byte(link, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    i2c_master_write_byte(link, cmd, true);
    i2c_master_stop(link);
    esp_err_t ret = i2c_master_cmd_begin(s_port, link, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(link);
    return ret;
}

static esp_err_t write_cmd_seq(const uint8_t *cmds, size_t len)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    if (link == NULL) { ESP_LOGE(TAG, "i2c_cmd_link_create OOM"); return ESP_ERR_NO_MEM; }
    i2c_master_start(link);
    i2c_master_write_byte(link, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    for (size_t i = 0; i < len; i++) {
        i2c_master_write_byte(link, cmds[i], true);
    }
    i2c_master_stop(link);
    esp_err_t ret = i2c_master_cmd_begin(s_port, link, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(link);
    return ret;
}

// Rate-limit OLED warnings to ~1 per second so a missing/disabled display
// does not flood the console every refresh cycle (Reviewer P1).
static bool oled_warn_allowed(void)
{
    static uint32_t s_last_ms = 0;
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t elapsed = now - s_last_ms;   // unsigned subtraction: wrap-safe
    if (elapsed >= 1000U) { s_last_ms = now; return true; }
    return false;
}

// Send up to 128 bytes per I2C transaction (≈ 11.5 ms at 100 kHz). The full
// 1024-byte buffer is sent in 8 page-sized chunks. On the FIRST chunk failure
// we stop immediately and return the real error — the caller decides whether
// to back off (Reviewer P1). We do NOT silently swallow I2C errors.
static esp_err_t write_data(const uint8_t *data, size_t len)
{
    const size_t chunk = 128;
    size_t offset = 0;
    while (offset < len) {
        i2c_cmd_handle_t link = i2c_cmd_link_create();
        if (link == NULL) {
            ESP_LOGE(TAG, "i2c_cmd_link_create OOM at offset %u",
                     (unsigned)offset);
            return ESP_ERR_NO_MEM;
        }
        i2c_master_start(link);
        i2c_master_write_byte(link, (s_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(link, 0x40, true);
        size_t this = (len - offset < chunk) ? len - offset : chunk;
        i2c_master_write(link, data + offset, this, true);
        i2c_master_stop(link);
        esp_err_t ret = i2c_master_cmd_begin(s_port, link, pdMS_TO_TICKS(30));
        i2c_cmd_link_delete(link);
        if (ret != ESP_OK) {
            if (oled_warn_allowed()) {
                ESP_LOGW(TAG, "write_data chunk %u failed: %s",
                         (unsigned)(offset / chunk), esp_err_to_name(ret));
            }
            return ret;   // stop on first failure, propagate real error
        }
        offset += this;
    }
    return ESP_OK;
}

// ── Init Sequence ────────────────────────────────────────────────────
esp_err_t ssd1306_init(i2c_port_t port, uint8_t addr_7bit,
                       gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_port = port;
    s_addr = addr_7bit;

    // Install I2C master driver.
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = sda_gpio,
        .scl_io_num       = scl_gpio,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = SSD1306_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install: %s", esp_err_to_name(ret));
        return ret;
    }

    // SSD1306 init sequence.
    // Display off
    if (write_cmd(0xAE) != ESP_OK) goto fail_i2c;
    // Set display clock divide: suggested ratio 0x80
    {   const uint8_t seq[] = {0xD5, 0x80};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Set multiplex ratio: 64 rows (0x3F)
    {   const uint8_t seq[] = {0xA8, 0x3F};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Set display offset: 0
    {   const uint8_t seq[] = {0xD3, 0x00};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Set start line: 0
    if (write_cmd(0x40) != ESP_OK) goto fail_i2c;
    // Enable charge pump
    {   const uint8_t seq[] = {0x8D, 0x14};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Set memory mode: horizontal
    {   const uint8_t seq[] = {0x20, 0x00};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Segment re-map: column 127 = SEG0 (0xA1)
    if (write_cmd(0xA1) != ESP_OK) goto fail_i2c;
    // COM scan direction: remapped (0xC8, scan from COM[N-1] to COM0)
    if (write_cmd(0xC8) != ESP_OK) goto fail_i2c;
    // COM pins hardware configuration: alternative config (0x12)
    {   const uint8_t seq[] = {0xDA, 0x12};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Set contrast: 0x7F (mid)
    {   const uint8_t seq[] = {0x81, 0x7F};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Pre-charge period: 0xF1
    {   const uint8_t seq[] = {0xD9, 0xF1};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // VCOM detect: 0x40
    {   const uint8_t seq[] = {0xDB, 0x40};
        if (write_cmd_seq(seq, 2) != ESP_OK) goto fail_i2c; }
    // Display all on resume (0xA4)
    if (write_cmd(0xA4) != ESP_OK) goto fail_i2c;
    // Normal display (0xA6, not inverted)
    if (write_cmd(0xA6) != ESP_OK) goto fail_i2c;
    // Display on
    if (write_cmd(0xAF) != ESP_OK) goto fail_i2c;

    // Clear buffer and display. Mark initialised BEFORE the flush so the
    // clear screen actually executes (ssd1306_flush() is a no-op before
    // s_initialized — Reviewer P1 contract fix).
    memset(s_buffer, 0, sizeof(s_buffer));
    s_initialized = true;
    esp_err_t flush_ret = ssd1306_flush();
    if (flush_ret != ESP_OK) {
        ESP_LOGE(TAG, "init clear flush failed: %s — deinitialising",
                 esp_err_to_name(flush_ret));
        // Roll back: de-init so caller can retry (contract: "On failure, no
        // I2C resources are left allocated").
        s_initialized = false;
        i2c_driver_delete(s_port);
        s_port = -1;
        s_addr = 0;
        return flush_ret;
    }
    ESP_LOGI(TAG, "init ok (port=%d, addr=0x%02X)", (int)port, addr_7bit);
    return ESP_OK;

fail_i2c:
    ESP_LOGE(TAG, "I2C command failed during init sequence");
    i2c_driver_delete(port);
    return ESP_FAIL;
}

void ssd1306_deinit(void)
{
    if (!s_initialized) return;
    // Display off
    write_cmd(0xAE);
    i2c_driver_delete(s_port);
    s_initialized = false;
    s_port = -1;
    ESP_LOGI(TAG, "deinit");
}

bool ssd1306_is_initialized(void)
{
    return s_initialized;
}

// ── Display Operations ───────────────────────────────────────────────
void ssd1306_clear_buffer(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

void ssd1306_clear(void)
{
    ssd1306_clear_buffer();
    ssd1306_flush();
}

esp_err_t ssd1306_flush(void)
{
    if (!s_initialized) return ESP_OK;   // safe no-op before init

    // Set column range 0..127. If this fails, do NOT write data.
    {   const uint8_t seq[] = {0x21, 0, 127};
        esp_err_t r = write_cmd_seq(seq, 3);
        if (r != ESP_OK) {
            if (oled_warn_allowed()) ESP_LOGW(TAG, "flush: set column range failed");
            return r;
        } }
    // Set page range 0..7.
    {   const uint8_t seq[] = {0x22, 0, 7};
        esp_err_t r = write_cmd_seq(seq, 3);
        if (r != ESP_OK) {
            if (oled_warn_allowed()) ESP_LOGW(TAG, "flush: set page range failed");
            return r;
        } }

    return write_data(s_buffer, sizeof(s_buffer));
}

uint8_t ssd1306_draw_char(uint8_t page, uint8_t col, char ch)
{
    if (!s_initialized) return col;
    if (page >= SSD1306_PAGES) return col;
    if (col >= SSD1306_WIDTH)  return col;

    // Clamp character to font range
    uint8_t idx;
    if (ch < 0x20 || ch > 0x7E) {
        idx = 0;  // render as space
    } else {
        idx = (uint8_t)(ch - 0x20);
    }

    const uint8_t *glyph = s_font_6x8 + (idx * 6);
    uint32_t offset = (uint32_t)page * SSD1306_WIDTH + col;
    uint8_t avail = SSD1306_WIDTH - col;
    uint8_t copy = (avail > 6) ? 6 : avail;
    for (uint8_t i = 0; i < copy; i++) {
        s_buffer[offset + i] = glyph[i];
    }
    return col + 6;
}

void ssd1306_draw_text(uint8_t page, uint8_t col, const char *text)
{
    while (*text != '\0') {
        uint8_t next = ssd1306_draw_char(page, col, *text);
        if (next <= col) break;  // overflow
        col = next;
        text++;
    }
}
