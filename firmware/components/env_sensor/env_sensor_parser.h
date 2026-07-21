// PrivacySense Matter Room Hub - DHT22 symbol parser (internal header)
//
// Self-contained (no ESP-IDF dependencies). Uses distinct type names
// (dht22_* prefix) to avoid collision with env_sensor.h types.
// env_sensor.c converts rmt_symbol_word_t → dht22_symbol_t before calling.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform-neutral RMT symbol — same fields as ESP-IDF rmt_symbol_word_t.
typedef struct {
    uint16_t duration0;
    uint16_t duration1;
    uint8_t  level0;
    uint8_t  level1;
} dht22_symbol_t;

// Parser result status (independent of env_sensor_failure_t).
typedef enum {
    DHT22_OK          = 0,
    DHT22_FAIL_TIMEOUT,
    DHT22_FAIL_PROTOCOL,
    DHT22_FAIL_RANGE,
} dht22_status_t;

// Parsed sample (independent of env_sensor_data_t).
typedef struct {
    uint32_t timestamp_ms;
    int16_t  temperature_cc;
    uint16_t humidity_permil;
    uint16_t co2_ppm;
    bool     valid;
    dht22_status_t failure;
} dht22_sample_t;

// DHT22 valid range constants.
#define DHT22_TEMP_MIN_CC   (-4000)
#define DHT22_TEMP_MAX_CC   ( 8000)
#define DHT22_HUMID_MIN_PM  (0)
#define DHT22_HUMID_MAX_PM  (1000)
#define DHT22_BIT_THRESHOLD_TICKS  40U

// Parse 40-bit DHT22 frame from RMT RX symbols.
// Returns DHT22_OK on success and fills *out.
dht22_status_t dht22_parse_symbols(const dht22_symbol_t *items, size_t num,
                                    dht22_sample_t *out);

#ifdef __cplusplus
}
#endif
