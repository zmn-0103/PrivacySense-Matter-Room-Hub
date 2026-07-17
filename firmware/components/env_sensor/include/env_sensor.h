// PrivacySense Matter Room Hub - environment sensor driver
//
// AM2302 / DHT22 single-wire temperature + humidity sensor on GPIO2.
// Hardware (connection-table.md §4.1):
//   - VDD = 3.3 V (after 2 s power-on settle)
//   - DATA pulled up to 3.3 V via ~5.1 kΩ external resistor
//   - Cable length ≤ 1 m
//
// Firmware rules (state-model.md §4.2, task-architecture.md §4.2):
//   - Sample period 5 s (≥ 2 s minimum per DHT22 datasheet)
//   - Use ESP-IDF RMT RX to capture the 40-bit pulse-width stream
//   - NO ISR parsing, NO long-critical-section busy-wait, NO STM32 bit-bang
//   - Consecutive 3 read failures → env_sensor_online=false
//
// Ownership (task-architecture.md §4.2, §5.1):
//   - Owns sensor_env_task and the DHT22 single-wire bus + RMT RX channel.
//   - Does NOT hold any FreeRTOS queue handle. Parsed samples are delivered
//     via a caller-registered callback on sensor_env_task context.
//
// SCD40 (CO2, I2C 0x62) is NOT driven by this component in v1. If a future
// revision adds SCD40, it must be a separate component / task to preserve the
// single-writer rule for the I2C bus (task-architecture.md §6.1).

#pragma once

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// MUST match main/state_machine.h::env_sensor_data_t exactly.
// `co2_ppm` is always 0 in v1 (SCD40 not populated); kept in the struct so
// later revisions can fill it without breaking the state-machine ABI.
typedef struct {
    uint32_t timestamp_ms;
    int16_t  temperature_cc;     // centi-celsius (1/100 °C, e.g. 2350 = 23.50 °C)
    uint16_t humidity_permil;    // per-mille (0..1000, e.g. 543 = 54.3 %RH)
    uint16_t co2_ppm;            // 0 in v1 (SCD40 not populated)
    bool     valid;
} env_sensor_data_t;

// Data callback. Invoked on sensor_env_task context for every sample attempt:
//   - valid=true  → successful read, fields populated
//   - valid=false → read failed (timeout / checksum / range); fields zeroed.
// The callback MUST NOT block; typical impl sends an app_event_t to
// g_app_event_queue with zero-wait or bounded-wait (≤ 20 ms).
typedef void (*env_sensor_data_callback_t)(const env_sensor_data_t *data);

// --- Lifecycle ---
// Initialise GPIO2 + RMT RX channel for DHT22, then spawn sensor_env_task
// (stack 4096, prio 4, period 5 s, see task-architecture.md §4.2).
//
// `data_gpio`     GPIO connected to DHT22 DATA pin (hardware: GPIO2).
// `rmt_clk_hz`    RMT tick resolution (hardware: 1 MHz → 1 µs tick).
// `callback`      Non-NULL; invoked for each sample (success or failure).
esp_err_t env_sensor_start(gpio_num_t data_gpio,
                           uint32_t rmt_clk_hz,
                           env_sensor_data_callback_t callback);

esp_err_t env_sensor_stop(void);   // TODO

#ifdef __cplusplus
}
#endif
