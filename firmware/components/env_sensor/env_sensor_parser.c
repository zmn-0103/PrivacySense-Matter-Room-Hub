// PrivacySense Matter Room Hub - DHT22 symbol parser implementation
//
// Extracted from env_sensor.c for independent host-side testing.
// Uses dht22_* types independent of env_sensor.h types.

#include "env_sensor_parser.h"

static bool decode_40_bits(const dht22_symbol_t *items, uint8_t data[5])
{
    for (int bit = 0; bit < 40; bit++) {
        const dht22_symbol_t *sym = &items[bit];
        bool val;
        if (sym->level0 == 0 && sym->level1 == 1) {
            val = (sym->duration1 > DHT22_BIT_THRESHOLD_TICKS);
        } else if (sym->level0 == 1 && sym->level1 == 0) {
            val = (sym->duration0 > DHT22_BIT_THRESHOLD_TICKS);
        } else {
            return false;
        }
        int bi = bit / 8;
        int bj = 7 - (bit % 8);
        if (val) data[bi] |= (1 << bj);
    }
    return true;
}

dht22_status_t dht22_parse_symbols(const dht22_symbol_t *items, size_t num,
                                    dht22_sample_t *out)
{
    if (num < 41) return DHT22_FAIL_PROTOCOL;

    bool range_seen = false;
    size_t max_start = num - 40;
    for (size_t start = 0; start <= max_start; start++) {
        uint8_t data[5] = {0};
        if (!decode_40_bits(&items[start], data)) continue;
        if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3]))
            continue;

        uint16_t humid = ((uint16_t)data[0] << 8) | data[1];
        uint16_t raw_t = ((uint16_t)data[2] << 8) | data[3];
        int32_t temp_cc = (raw_t & 0x8000)
                          ? -((int32_t)(raw_t & 0x7FFF) * 10)
                          : (int32_t)raw_t * 10;

        if (humid > DHT22_HUMID_MAX_PM ||
            temp_cc < DHT22_TEMP_MIN_CC ||
            temp_cc > DHT22_TEMP_MAX_CC) {
            range_seen = true;
            continue;
        }

        out->humidity_permil = humid;
        out->temperature_cc  = (int16_t)temp_cc;
        out->co2_ppm         = 0;
        out->failure         = DHT22_OK;
        out->valid           = true;
        return DHT22_OK;
    }
    return range_seen ? DHT22_FAIL_RANGE : DHT22_FAIL_PROTOCOL;
}
