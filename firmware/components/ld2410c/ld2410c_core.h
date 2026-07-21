// PrivacySense Matter Room Hub - LD2410C transaction state machine (core)
//
// Pure-C, FreeRTOS-independent command/transaction engine. It owns the ACK
// re-synchronisation, timeout, status and command-match checks, and the
// atomic ENABLE -> BUSINESS -> DISABLE config transaction. The byte transport
// is injected (see ld2410c_transport_t) so the logic can be unit-tested on the
// host with a fake transport (test_ld2410c_transaction.c) — this is required
// because the FreeRTOS-queue glue cannot run off-target.
//
// The production driver (ld2410c.c) provides a UART transport and wraps this
// core behind a mutex + by-value request/response queues.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ld2410c_parser.h"

#ifdef LD2410C_CORE_HOST
// Host unit-test build: provide a minimal esp_err_t shim (no ESP-IDF).
#include "ld2410c_core_host_compat.h"
#else
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ── Transaction detail (optional output from exec_transaction) ──────────
typedef struct {
    esp_err_t enable_result;
    esp_err_t business_result;
    bool      disable_attempted;
    esp_err_t disable_result;
} ld2410c_transaction_detail_t;

// ── Abstract byte transport (injected by the caller) ───────────────────────
typedef struct {
    // Send exactly `len` bytes. Return bytes sent (>0) or <0 on error.
    int (*send)(void *ctx, const uint8_t *buf, int len);
    // Receive up to `cap` bytes, waiting at most `timeout_ms` for data.
    // Return bytes received (>0), 0 on timeout, <0 on error.
    int (*recv)(void *ctx, uint8_t *buf, int cap, int timeout_ms);
    // Optional: discard buffered RX before a new command. May be NULL.
    void (*flush)(void *ctx);
    // Optional: return data to the front of the RX stream. May be NULL.
    void (*put_back)(void *ctx, const uint8_t *data, int len);
    void *ctx;
} ld2410c_transport_t;

// Monotonic millisecond clock. Must wrap gracefully under unsigned subtraction.
typedef uint32_t (*ld2410c_clock_ms_t)(void);

// Execute ONE command: send frame, re-sync to ACK, validate, copy ACK payload.
// Returns:
//   ESP_OK                   success (status==0, cmd matched)
//   ESP_ERR_TIMEOUT          no complete ACK within timeout_ms
//   ESP_ERR_INVALID_RESPONSE ACK status != 0
//   ESP_FAIL                 malformed ACK / cmd mismatch / send error
//   ESP_ERR_INVALID_ARG      NULL args
//   ESP_ERR_NO_MEM           frame buffer too small
//   ESP_ERR_INVALID_SIZE     ACK payload exceeds rx_cap
esp_err_t ld2410c_core_exec_cmd(const ld2410c_transport_t *t,
                                ld2410c_clock_ms_t clock,
                                uint16_t cmd_word,
                                const uint8_t *tx_data, int tx_data_len,
                                uint8_t *rx_buf, int rx_cap, int *rx_out_len,
                                uint32_t timeout_ms);

// Atomic config transaction: ENABLE -> BUSINESS -> DISABLE.
// Return semantics (Reviewer P0 #3 fix):
//   enable fails               -> enable error (disable NOT attempted)
//   business fails, disable OK -> business error (clean exit from config)
//   business fails, disable FAIL -> ESP_FAIL (radar state UNCERTAIN)
//   business OK, disable OK    -> ESP_OK
//   business OK, disable FAIL  -> ESP_FAIL (radar state UNCERTAIN; caller must
//                                   mark offline / run recovery)
// The business ACK payload is copied into rx_buf on success.
// When `detail` is non-NULL, it is filled with per-step results for logging.
esp_err_t ld2410c_core_exec_transaction(const ld2410c_transport_t *t,
                                        ld2410c_clock_ms_t clock,
                                        uint16_t business_cmd,
                                        const uint8_t *tx_data, int tx_data_len,
                                        uint8_t *rx_buf, int rx_cap, int *rx_out_len,
                                        uint32_t timeout_ms,
                                        ld2410c_transaction_detail_t *detail);

#ifdef __cplusplus
}
#endif
