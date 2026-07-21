// PrivacySense Matter Room Hub - pins.h
//
// Frozen GPIO / peripheral pin assignments.
// Source of truth: hardware/connection-table.md (frozen v0.3, 2026-07-17).
//
// Rule (AGENTS.md section 4): GPIO, UART, I2C addresses and supply voltages
// MUST live in a connection table or config file, not scattered as hardcodes.
// This header is the firmware-side mirror of that table. Do NOT redefine these
// constants elsewhere; if the table changes, update BOTH files in the same commit.

#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// DHT22 / AM2302 (GPIO 2) — temperature + humidity, single-wire, RMT capture
// ---------------------------------------------------------------------------
// Hardware (connection-table.md §4.1):
//   - VDD = 3.3 V (after 2 s power-on settle)
//   - DATA pulled up to 3.3 V via ~5.1 kΩ external resistor
//   - Cable length ≤ 1 m
// Firmware rules (state-model.md §4.2, task-architecture.md §4.2):
//   - Min sample interval 2 s; first version fixed at 5 s
//   - Use ESP-IDF RMT RX to capture 40-bit pulse widths
//   - NO ISR parsing, NO long-critical-section busy-wait ports from STM32
//   - Consecutive 3 read failures → env_sensor_online=false
#define PIN_DHT22_DATA_GPIO     GPIO_NUM_2
#define PIN_DHT22_SAMPLE_MS     5000U     // 5 s, must be ≥ 2000
#define PIN_DHT22_RMT_RX_CHAN   0         // RMT channel for RX
#define PIN_DHT22_RMT_CLK_HZ    1000000U  // 1 MHz → 1 µs tick

// ---------------------------------------------------------------------------
// I2C bus 0 (GPIO 6 / 7) — SSD1306 OLED + SCD40 (optional CO2)
// ---------------------------------------------------------------------------
// NOTE: BME280/SHT31 are NOT used in v1; environment sensing is via DHT22
// (single-wire on GPIO 2). I2C bus 0 is for OLED display only in v1, with
// SCD40 as a future optional extension.
#define PIN_I2C0_SDA_GPIO       GPIO_NUM_6
#define PIN_I2C0_SCL_GPIO       GPIO_NUM_7
#define PIN_I2C0_PORT           I2C_NUM_0
#define PIN_I2C0_FREQ_HZ        100000U   // 100 kHz standard mode (connection-table.md §3.1)
// External 4.7 kΩ pull-ups to 3.3 V required only if OLED module lacks them.

// I2C device addresses (7-bit). Confirm via I2C bus scan on first power-on.
#define PIN_I2C_ADDR_SSD1306    0x3C      // 0x3D if SA0=VDD; verify on real module
#define PIN_I2C_ADDR_SCD40      0x62      // Optional, fixed, not configurable

// ---------------------------------------------------------------------------
// UART1 (GPIO 4 / 5) — ESP32-C6 ↔ HLK-LD2410C-P radar
// ---------------------------------------------------------------------------
// U0TXD/U0RXD (GPIO 16/17) are reserved for the on-board USB-to-UART console
// and MUST NOT be reused for the radar. See connection-table.md §1.
#define PIN_RADAR_UART_NUM      UART_NUM_1
#define PIN_RADAR_UART_TX_GPIO  GPIO_NUM_4   // ESP32-C6 → LD2410C RX
#define PIN_RADAR_UART_RX_GPIO  GPIO_NUM_5   // LD2410C TX → ESP32-C6
#define PIN_RADAR_OUT_GPIO      GPIO_NUM_3   // Optional radar OUT pin (active high)
#define PIN_RADAR_UART_BAUD     256000U      // LD2410C default
#define PIN_RADAR_UART_DATA_BIT 8
#define PIN_RADAR_UART_STOP_BIT 1
#define PIN_RADAR_UART_PARITY   UART_PARITY_DISABLE
#define PIN_RADAR_UART_FLOW_CTL UART_HW_FLOWCTRL_DISABLE

// ---------------------------------------------------------------------------
// RGB LED (GPIO 8) — DevKitC-1 on-board WS2812
// ---------------------------------------------------------------------------
// GPIO 8 is a Strapping pin. The on-board RGB LED is the only allowed load on
// this pin; never attach external circuitry that could perturb the power-on level.
#define PIN_RGB_LED_GPIO        GPIO_NUM_8
#define PIN_RGB_LED_NUM         1
#define PIN_RGB_LED_RMT_CHAN    0           // RMT channel used by led_strip driver

// ---------------------------------------------------------------------------
// User button (GPIO 9) — DevKitC-1 Boot button, active low
// ---------------------------------------------------------------------------
// GPIO 9 is a Strapping pin. Internal pull-up is enabled at boot; the button
// pulls the pin low when pressed. Boot-time level MUST remain high.
#define PIN_BUTTON_GPIO         GPIO_NUM_9
#define PIN_BUTTON_ACTIVE_LOW   true
#define PIN_BUTTON_PULL         GPIO_PULLUP_ENABLE

// ---------------------------------------------------------------------------
// Optional expansion (NOT populated in v1; do not initialise)
// ---------------------------------------------------------------------------
// GPIO 10 — LM393 photoresistor module DO (digital threshold only, not lux).
//           Must be powered at 3.3 V; logic polarity must be measured.
//           NIGHT mode in v1 stays on SNTP time window, not on this signal.
#define PIN_PHOTO_LM393_DO_GPIO GPIO_NUM_10   // Optional, not initialised in v1

// ---------------------------------------------------------------------------
// Strapping pins (ESP32-C6 Technical Reference Manual)
// ---------------------------------------------------------------------------
// All Strapping pins:
//   GPIO 4  (MTMS) — used as UART1 TX for radar; verify cold-boot level
//   GPIO 5  (MTDI) — used as UART1 RX for radar; verify cold-boot level
//   GPIO 8  (GPIO) — RGB LED (only allowed load on this pin)
//   GPIO 9  (GPIO) — Boot button (internal pull-up ensures high at boot)
//   GPIO 15 (GPIO) — unused in first version
//
// JTAG pins (GPIO 4-7):
//   GPIO 4 (MTMS), GPIO 5 (MTDI), GPIO 6 (MTCK), GPIO 7 (MTDO)
//   GPIO 6/7 used for I2C bus 0 (OLED). JTAG function is not used in v1.
//
// Reserved:
//   GPIO 12 / 13 — USB D-/D+ (on-board USB Serial/JTAG, default)
//   GPIO 16 / 17 — U0TXD/U0RXD, reserved for console / download

#ifdef __cplusplus
}
#endif
