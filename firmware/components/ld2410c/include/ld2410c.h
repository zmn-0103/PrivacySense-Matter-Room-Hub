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
//   - The driver owns sensor_radar_task and the UART1 resource. The UART is
//     NEVER deleted during a config transaction.
//   - Config commands travel through driver-owned FreeRTOS queues BY VALUE
//     (command_request_t / command_response_t) plus a transaction mutex that
//     also excludes ld2410c_stop(). No caller-stack pointers or caller
//     semaphores cross the queue boundary, so results propagate correctly and
//     there is no use-after-free on timeout.
//   - Parsed normal-mode frames are delivered via a caller-registered
//     callback invoked on sensor_radar_task context; it MUST be non-blocking
//     (typical impl: wrap the data in an app_event_t and xQueueSend).

#pragma once

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"

// Shared parser types and functions (self-contained, no ESP-IDF deps).
#include "../ld2410c_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// Stop the driver and delete the task.
esp_err_t ld2410c_stop(void);

// ── Configuration (synchronous, blocking, type-safe) ────────────────
//
// Every command is wrapped in an atomic ENABLE -> BUSINESS -> DISABLE
// transaction executed by sensor_radar_task. The calls block until the
// transaction completes or times out. Intended for occasional use
// (commissioning, diagnostics) — NOT the normal sensing path.
//
// Returns ESP_OK on success. On timeout/protocol error the radar state may be
// left UNCERTAIN (see ld2410c_radar_state_uncertain()); the caller should run
// recovery (e.g. re-read params or restart the module).

// Read all parameters (0x0061). Fills *out.
esp_err_t ld2410c_read_params(ld2410c_read_params_t *out);

// Write basic config (0x0060): max moving/static distance gate + unoccupied
// delay. Values are range-checked; invalid input returns ESP_ERR_INVALID_ARG.
esp_err_t ld2410c_write_basic_params(const ld2410c_basic_params_t *params);

// Set gate sensitivity (0x0064). gate: 0..8 or LD2410C_GATE_ALL (0xFFFF).
esp_err_t ld2410c_set_gate_sensitivity(uint16_t gate,
                                       uint8_t moving, uint8_t stationary);

// Low-level: send a raw command (wrapped in the atomic transaction) and copy
// the ACK payload into rx_buf. Provided for diagnostics / future commands.
esp_err_t ld2410c_exec_cmd(uint16_t cmd_word,
                           const uint8_t *tx_data, int tx_data_len,
                           uint8_t *rx_buf, int rx_cap, int *rx_out_len);

// True if the last config transaction left the radar in an uncertain state
// (e.g. business OK but DISABLE_CONFIG failed). Caller should recover.
bool ld2410c_radar_state_uncertain(void);

#ifdef __cplusplus
}
#endif
