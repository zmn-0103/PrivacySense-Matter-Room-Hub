// PrivacySense Matter Room Hub - LD2410C transaction state machine (core)
//
// Pure-C implementation of ld2410c_core.h. No FreeRTOS / ESP-IDF dependencies
// (only the host-compat esp_err_t shim when built off-target). See that file
// for the contract.

#include "ld2410c_core.h"

#include <string.h>

// ── Single command execution with ACK re-synchronisation ──────────────────
esp_err_t ld2410c_core_exec_cmd(const ld2410c_transport_t *t,
                                ld2410c_clock_ms_t clock,
                                uint16_t cmd_word,
                                const uint8_t *tx_data, int tx_data_len,
                                uint8_t *rx_buf, int rx_cap, int *rx_out_len,
                                uint32_t timeout_ms)
{
    if (t == NULL || t->send == NULL || t->recv == NULL || clock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rx_out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rx_cap < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rx_buf == NULL && rx_cap > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tx_data_len < 0 || tx_data_len > LD2410C_CMD_MAX_DATA) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tx_data_len > 0 && tx_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_frame[LD2410C_CMD_MAX_FRAME];
    int tx_len;
    if (ld2410c_build_cmd_frame(tx_frame, sizeof(tx_frame), &tx_len,
                                cmd_word, tx_data, tx_data_len) == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (t->flush != NULL) t->flush(t->ctx);

    int sent = t->send(t->ctx, tx_frame, tx_len);
    if (sent != tx_len) {
        return ESP_FAIL;
    }

    // Rolling RX buffer (double-sized so a stray normal-mode frame + ACK fit).
    uint8_t rb[LD2410C_CMD_MAX_FRAME * 2];
    int rb_len = 0;
    uint32_t start = clock();

    while ((uint32_t)(clock() - start) < timeout_ms) {
        int cap = (int)sizeof(rb) - rb_len;
        if (cap <= 0) {
            // Garbage overflow: keep only the last 3 bytes and re-sync.
            int keep = (rb_len > 3) ? 3 : rb_len;
            if (keep < rb_len) memmove(rb, rb + rb_len - keep, keep);
            rb_len = keep;
            cap = (int)sizeof(rb) - rb_len;
        }

        int n = t->recv(t->ctx, rb + rb_len, cap, 50);
        if (n < 0) return ESP_FAIL;   // transport error
        if (n > 0) rb_len += n;

        // Re-synchronise: locate the command head FD FC FB FA.
        int off = ld2410c_find_cmd_head(rb, rb_len);
        if (off < 0) {
            // No head yet; retain only the last 3 bytes (head may be split).
            if (rb_len > 3) {
                memmove(rb, rb + rb_len - 3, 3);
                rb_len = 3;
            }
            continue;
        }
        if (off > 0) {
            // Drop stale bytes (normal-mode report / noise) before the head.
            memmove(rb, rb + off, rb_len - off);
            rb_len -= off;
        }

        int sz = ld2410c_cmd_frame_size(rb, rb_len);
        if (sz > 0) {
            // Complete frame at offset 0.
            if (!ld2410c_cmd_frame_valid(rb, sz)) {
                memmove(rb, rb + 1, rb_len - 1);   // malformed: advance 1
                rb_len -= 1;
                continue;
            }
            if (!ld2410c_cmd_ack_matches(rb, sz, cmd_word)) {
                memmove(rb, rb + 1, rb_len - 1);   // wrong cmd: treat as noise
                rb_len -= 1;
                continue;
            }
            uint16_t status = (uint16_t)rb[8] | ((uint16_t)rb[9] << 8);
            if (status != 0) {
                // Put back leftover bytes before returning so the next
                // exec_cmd can consume them (Reviewer P0 #3 fix).
                int leftover = rb_len - sz;
                if (leftover > 0 && t->put_back != NULL) {
                    t->put_back(t->ctx, rb + sz, leftover);
                }
                return ESP_ERR_INVALID_RESPONSE;
            }
            uint16_t data_field_len = (uint16_t)rb[4] | ((uint16_t)rb[5] << 8);
            int ack_data_len = (int)data_field_len - LD2410C_CMD_WORD_LEN
                               - LD2410C_CMD_STATUS_LEN;
            if (ack_data_len < 0) ack_data_len = 0;
            if (ack_data_len > rx_cap) {
                *rx_out_len = ack_data_len;  // report needed capacity
                // Put back leftover bytes before returning (Reviewer P1).
                int leftover = rb_len - sz;
                if (leftover > 0 && t->put_back != NULL) {
                    t->put_back(t->ctx, rb + sz, leftover);
                }
                return ESP_ERR_INVALID_SIZE;
            }
            int copy = ack_data_len;
            if (copy > 0 && rx_buf != NULL) memcpy(rx_buf, rb + 10, copy);
            *rx_out_len = copy;
            // Put back leftover bytes so the next exec_cmd can consume them.
            // (e.g. when the UART delivers a second ACK frame in the same read batch.)
            int leftover = rb_len - sz;
            if (leftover > 0 && t->put_back != NULL) {
                t->put_back(t->ctx, rb + sz, leftover);
            }
            return ESP_OK;
        }
        if (sz < 0) {
            // Invalid at head; advance past it and keep re-syncing.
            memmove(rb, rb + 1, rb_len - 1);
            rb_len -= 1;
        }
        // sz == 0: NEED_MORE — keep reading.
    }

    return ESP_ERR_TIMEOUT;
}

// ── Atomic config transaction ─────────────────────────────────────────────
esp_err_t ld2410c_core_exec_transaction(const ld2410c_transport_t *t,
                                        ld2410c_clock_ms_t clock,
                                        uint16_t business_cmd,
                                        const uint8_t *tx_data, int tx_data_len,
                                        uint8_t *rx_buf, int rx_cap, int *rx_out_len,
                                        uint32_t timeout_ms,
                                        ld2410c_transaction_detail_t *detail)
{
    if (t == NULL) return ESP_ERR_INVALID_ARG;

    // Step 1: ENABLE_CONFIG (value 0x0001)
    uint8_t enable_val[2] = { 0x01, 0x00 };
    uint8_t en_ack[16];
    int en_len = 0;
    esp_err_t en_ret = ld2410c_core_exec_cmd(t, clock, LD2410C_CMD_ENABLE_CONF,
                                             enable_val, sizeof(enable_val),
                                             en_ack, sizeof(en_ack), &en_len,
                                             timeout_ms);
    if (en_ret != ESP_OK) {
        // Cannot enter config mode — do NOT attempt business/disable.
        if (detail) {
            detail->enable_result    = en_ret;
            detail->business_result  = ESP_FAIL;
            detail->disable_attempted = false;
            detail->disable_result   = ESP_OK;
        }
        return en_ret;
    }

    // Step 2: business command
    esp_err_t biz_ret = ld2410c_core_exec_cmd(t, clock, business_cmd,
                                              tx_data, tx_data_len,
                                              rx_buf, rx_cap, rx_out_len,
                                              timeout_ms);

    // Step 3: DISABLE_CONFIG (always attempt, best effort)
    uint8_t dis_ack[8];
    int dis_len = 0;
    esp_err_t dis_ret = ld2410c_core_exec_cmd(t, clock, LD2410C_CMD_DISABLE_CONF,
                                              NULL, 0,
                                              dis_ack, sizeof(dis_ack), &dis_len,
                                              timeout_ms);

    if (detail) {
        detail->enable_result    = en_ret;
        detail->business_result  = biz_ret;
        detail->disable_attempted = true;
        detail->disable_result   = dis_ret;
    }

    // Combined return logic (Reviewer P0 #3):
    //   biz fail + disable fail → ESP_FAIL (uncertain)
    //   biz fail + disable OK   → biz_ret (clean exit)
    //   biz OK   + disable fail → ESP_FAIL (uncertain)
    //   biz OK   + disable OK   → ESP_OK
    if (biz_ret != ESP_OK && dis_ret != ESP_OK) {
        return ESP_FAIL;
    }
    if (biz_ret != ESP_OK) {
        return biz_ret;
    }
    if (dis_ret != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
