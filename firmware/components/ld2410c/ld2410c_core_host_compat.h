// Host-compat shim for ld2410c_core (used ONLY by off-target unit tests).
// Provides a minimal esp_err_t vocabulary so the core compiles without the
// ESP-IDF toolchain. Numeric values are chosen to be distinct; tests only
// compare against ESP_OK / ESP_FAIL etc., so exact codes do not matter.
#pragma once
#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                 -1
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_TIMEOUT          0x104
#define ESP_ERR_INVALID_RESPONSE 0x105
#define ESP_ERR_INVALID_SIZE     0x106
