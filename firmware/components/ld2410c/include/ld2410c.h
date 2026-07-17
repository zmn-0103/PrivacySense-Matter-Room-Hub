// PrivacySense Matter Room Hub - LD2410C-P radar driver
//
// UART protocol reference: "LD2410C 串口通信协议 V1.09.pdf" (2025-06-09).
// Hardware: HLK-LD2410C-P, 5 V supply, 3.3 V IO, default baud 256000.
//
// Only "normal mode" continuous reporting is parsed in production. Engineering
// mode (per-gate energy / distance) is used only for calibration and is not
// enabled by this driver.
//
// Ownership (task-architecture.md §4.1, §5.1):
//   - The driver owns sensor_radar_task and the UART1 resource.
//   - The driver does NOT hold any FreeRTOS queue handle. Parsed frames are
//     delivered via a caller-registered callback. The callback is invoked on
//     sensor_radar_task context and MUST be non-blocking (typical impl: wrap
//     the data in an app_event_t and xQueueSend to g_app_event_queue).

#pragma once

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration to avoid a circular include with main/state_machine.h.
// The struct layout here MUST match main/state_machine.h::ld2410c_radar_data_t
// exactly (state_machine.h includes this header for the typedef).
typedef struct {
    uint32_t timestamp_ms;
    bool     target_present;
    uint16_t moving_distance_cm;
    uint16_t static_distance_cm;
    uint8_t  moving_energy;
    uint8_t  static_energy;
    bool     valid;
} ld2410c_radar_data_t;

// Data callback. Invoked on sensor_radar_task context for every parsed frame
// (valid=true) and for every UART idle-timeout placeholder (valid=false).
// The callback MUST NOT block; typical impl sends an app_event_t to
// g_app_event_queue with zero-wait or bounded-wait (≤ 20 ms).
typedef void (*ld2410c_data_callback_t)(const ld2410c_radar_data_t *data);

// --- Lifecycle ---
// Initialise UART1 on the given pins + baud, then spawn sensor_radar_task
// (stack 4096, prio 5, see task-architecture.md §4.1). Parsed frames and
// idle-timeout placeholders are delivered via `callback`.
//
// `callback` MUST be non-NULL. The callback is stored by reference; the caller
// MUST keep the function alive for the lifetime of the driver.
esp_err_t ld2410c_start(uart_port_t uart_num,
                        gpio_num_t tx_gpio,
                        gpio_num_t rx_gpio,
                        uint32_t baud,
                        ld2410c_data_callback_t callback);

// Stop the driver and delete the task. Not yet implemented (TODO).
esp_err_t ld2410c_stop(void);

#ifdef __cplusplus
}
#endif
