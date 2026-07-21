// PrivacySense Matter Room Hub - DHT22 parser unit tests
//
// Tests the REAL production parser from env_sensor_parser.h / .c.
// No ESP-IDF dependencies — uses dht22_symbol_t directly.

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#include "../env_sensor_parser.h"

// =========================================================================
//  Test helpers
// =========================================================================

static size_t build_frame(dht22_symbol_t *out, size_t out_cap,
                          uint8_t b0, uint8_t b1, uint8_t b2,
                          uint8_t b3, uint8_t b4)
{
    (void)out_cap;
    size_t idx = 0;
    // Preamble symbol (falling edge, 80 µs)
    out[idx].duration0 = 80;
    out[idx].duration1 = 80;
    out[idx].level0 = 1;
    out[idx].level1 = 0;
    idx++;

    uint8_t bytes[5] = { b0, b1, b2, b3, b4 };
    for (int bit = 0; bit < 40; bit++) {
        int bi = bit / 8;
        int bj = 7 - (bit % 8);
        bool val = (bytes[bi] >> bj) & 1;
        out[idx].level0    = 0;
        out[idx].duration0 = 50;
        out[idx].level1    = 1;
        out[idx].duration1 = val ? 70 : 27;
        idx++;
    }
    return idx;
}

// =========================================================================
//  R09: DHT22 parser — valid frames
// =========================================================================

static void test_dht22_valid_normal(void)
{
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64, 0x02, 0x58, 0x00, 0xFD,
                           (uint8_t)(0x02 + 0x58 + 0x00 + 0xFD));
    dht22_sample_t out = {0};
    assert(dht22_parse_symbols(syms, n, &out) == DHT22_OK);
    assert(out.temperature_cc == 2530);
    assert(out.humidity_permil == 600);
    assert(out.co2_ppm == 0);
    printf("  PASS: DHT22-T1 valid normal (25.30 C, 60.0 %%RH)\n");
}

static void test_dht22_extreme_cold(void)
{
    uint16_t raw_temp = 0x8000 | 400;  // -40.0 C in 0.1C units (sign-mag)
    uint8_t cs = (uint8_t)(0x00 + 0x00 + (raw_temp >> 8) + (raw_temp & 0xFF));
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64,
                           (uint8_t)0x00, (uint8_t)0x00,
                           (uint8_t)(raw_temp >> 8), (uint8_t)(raw_temp & 0xFF),
                           cs);
    dht22_sample_t out = {0};
    assert(dht22_parse_symbols(syms, n, &out) == DHT22_OK);
    assert(out.temperature_cc == -4000);
    assert(out.humidity_permil == 0);
    printf("  PASS: DHT22-T2 extreme cold (-40.00 C, 0 %%RH)\n");
}

static void test_dht22_extreme_hot(void)
{
    uint16_t raw_temp = 800;
    uint8_t cs = (uint8_t)(0x03 + 0xE8 + (raw_temp >> 8) + (raw_temp & 0xFF));
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64,
                           (uint8_t)0x03, (uint8_t)0xE8,
                           (uint8_t)(raw_temp >> 8), (uint8_t)(raw_temp & 0xFF),
                           cs);
    dht22_sample_t out = {0};
    assert(dht22_parse_symbols(syms, n, &out) == DHT22_OK);
    assert(out.temperature_cc == 8000);
    assert(out.humidity_permil == 1000);
    printf("  PASS: DHT22-T3 extreme hot (80.00 C, 100 %%RH)\n");
}

static void test_dht22_negative_temp(void)
{
    uint16_t raw_temp = 0x8000 | 105;  // -10.5 C
    uint8_t cs = (uint8_t)(0x01 + 0x90 + (raw_temp >> 8) + (raw_temp & 0xFF));
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64,
                           (uint8_t)0x01, (uint8_t)0x90,
                           (uint8_t)(raw_temp >> 8), (uint8_t)(raw_temp & 0xFF),
                           cs);
    dht22_sample_t out = {0};
    assert(dht22_parse_symbols(syms, n, &out) == DHT22_OK);
    assert(out.temperature_cc == -1050);
    assert(out.humidity_permil == 400);
    printf("  PASS: DHT22-T4 negative temp (-10.50 C)\n");
}

// =========================================================================
//  DHT22 parser — boundary / failure cases
// =========================================================================

static void test_dht22_temp_below_min(void)
{
    uint16_t raw_temp = 0x8000 | 401;  // -40.1 C → -4010 cc < -4000
    uint8_t cs = (uint8_t)(0x00 + 0x00 + (raw_temp >> 8) + (raw_temp & 0xFF));
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64,
                           (uint8_t)0x00, (uint8_t)0x00,
                           (uint8_t)(raw_temp >> 8), (uint8_t)(raw_temp & 0xFF),
                           cs);
    assert(dht22_parse_symbols(syms, n, &(dht22_sample_t){0}) == DHT22_FAIL_RANGE);
    printf("  PASS: DHT22-T5 temp < -40 C -> FAIL_RANGE\n");
}

static void test_dht22_temp_above_max(void)
{
    uint16_t raw_temp = 801;  // 80.1 C → 8010 cc > 8000
    uint8_t cs = (uint8_t)(0x00 + 0x00 + (raw_temp >> 8) + (raw_temp & 0xFF));
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64,
                           (uint8_t)0x00, (uint8_t)0x00,
                           (uint8_t)(raw_temp >> 8), (uint8_t)(raw_temp & 0xFF),
                           cs);
    assert(dht22_parse_symbols(syms, n, &(dht22_sample_t){0}) == DHT22_FAIL_RANGE);
    printf("  PASS: DHT22-T6 temp > 80 C -> FAIL_RANGE\n");
}

static void test_dht22_humid_above_max(void)
{
    uint8_t cs = (uint8_t)(0x03 + 0xE9 + 0x01 + 0x90);
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64,
                           (uint8_t)0x03, (uint8_t)0xE9,  // 1001 > 1000
                           (uint8_t)0x01, (uint8_t)0x90,  // temp 40.0 C (valid)
                           cs);
    assert(dht22_parse_symbols(syms, n, &(dht22_sample_t){0}) == DHT22_FAIL_RANGE);
    printf("  PASS: DHT22-T7 humidity > 100%% -> FAIL_RANGE\n");
}

static void test_dht22_bad_checksum(void)
{
    dht22_symbol_t syms[64];
    size_t n = build_frame(syms, 64, 0x02, 0x58, 0x00, 0xFD, 0x00);
    assert(dht22_parse_symbols(syms, n, &(dht22_sample_t){0}) == DHT22_FAIL_PROTOCOL);
    printf("  PASS: DHT22-T8 bad checksum -> FAIL_PROTOCOL\n");
}

static void test_dht22_too_few_symbols(void)
{
    dht22_symbol_t syms[40];
    memset(syms, 0, sizeof(syms));
    assert(dht22_parse_symbols(syms, 40, &(dht22_sample_t){0}) == DHT22_FAIL_PROTOCOL);
    printf("  PASS: DHT22-T9 too few symbols (40 < 41) -> FAIL_PROTOCOL\n");
}

static void test_dht22_invalid_pattern(void)
{
    dht22_symbol_t syms[64];
    for (size_t i = 0; i < 41; i++) {
        syms[i].level0 = 0;
        syms[i].duration0 = 50;
        syms[i].level1 = 0;  // invalid edge (should be 1)
        syms[i].duration1 = 50;
    }
    assert(dht22_parse_symbols(syms, 41, &(dht22_sample_t){0}) == DHT22_FAIL_PROTOCOL);
    printf("  PASS: DHT22-T10 invalid symbol pattern -> FAIL_PROTOCOL\n");
}

static void test_dht22_sliding_window(void)
{
    dht22_symbol_t syms[64];
    syms[0].duration0 = 80;
    syms[0].duration1 = 80;
    syms[0].level0 = 1;
    syms[0].level1 = 1;  // invalid edge
    size_t n = 1 + build_frame(syms + 1, 63, 0x02, 0x58, 0x00, 0xFD,
                                (uint8_t)(0x02 + 0x58 + 0x00 + 0xFD));
    dht22_sample_t out = {0};
    assert(dht22_parse_symbols(syms, n, &out) == DHT22_OK);
    assert(out.temperature_cc == 2530);
    printf("  PASS: DHT22-T11 sliding window skips bad preamble\n");
}

// =========================================================================
//  Main test runner
// =========================================================================

void test_main(void)
{
    printf("\n=== DHT22 Parser Unit Tests (production parser) ===\n\n");

    printf("[DHT22] Valid frames:\n");
    test_dht22_valid_normal();
    test_dht22_extreme_cold();
    test_dht22_extreme_hot();
    test_dht22_negative_temp();

    printf("\n[DHT22] Boundary / failure cases:\n");
    test_dht22_temp_below_min();
    test_dht22_temp_above_max();
    test_dht22_humid_above_max();
    test_dht22_bad_checksum();
    test_dht22_too_few_symbols();
    test_dht22_invalid_pattern();
    test_dht22_sliding_window();

    printf("\n=== ALL TESTS PASSED ===\n\n");
}

int main(void)
{
    test_main();
    return 0;
}
