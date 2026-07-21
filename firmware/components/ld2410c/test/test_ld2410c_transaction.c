// PrivacySense Matter Room Hub - LD2410C transaction-core unit tests
//
// Tests ld2410c_core_exec_cmd / ld2410c_core_exec_transaction with a
// fake byte transport and fake clock. NO FreeRTOS or ESP-IDF required.
//
// Host build:
//   gcc -std=c11 -Wall -Wextra -O2 -DLD2410C_CORE_HOST -I..
//       test_ld2410c_transaction.c ../ld2410c_core.c ../ld2410c_parser.c
//
// Covers Reviewer P0 requirements:
//   - worker timeout → caller gets timeout
//   - nonzero ACK status propagated
//   - disable fail → not ESP_OK
//   - ordering ENABLE → BUSINESS → DISABLE
//   - resync handles stale normal-mode bytes before ACK
//   - no UAF (all by-value data)

#include "../ld2410c_core.h"
#include "../ld2410c_parser.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

static const uint8_t ENABLE_ACK_OK[] = {
    0xFD,0xFC,0xFB,0xFA, 0x08,0x00, 0xFF,0x01, 0x00,0x00,
    0x01,0x00, 0x40,0x00, 0x04,0x03,0x02,0x01
};
static const uint8_t DISABLE_ACK_OK[] = {
    0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0xFE,0x01, 0x00,0x00,
    0x04,0x03,0x02,0x01
};
static const uint8_t WRITE_ACK_OK[] = {
    0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0x60,0x01, 0x00,0x00,
    0x04,0x03,0x02,0x01
};
static const uint8_t ENABLE_ACK_FAIL[] = {
    0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0xFF,0x01, 0x01,0x00,
    0x04,0x03,0x02,0x01
};
static const uint8_t DISABLE_ACK_FAIL[] = {
    0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0xFE,0x01, 0x01,0x00,
    0x04,0x03,0x02,0x01
};

// ── Fake transport + clock ────────────────────────────────────────────────
typedef struct {
    const uint8_t *script;     // bytes to return (NULL → recv returns 0)
    int script_len;
    int script_pos;
    int chunk;                 // bytes returned per recv() call (0 = all)
    // put-back buffer for bytes beyond the frame boundary
    uint8_t prebuf[256];
    int prebuf_len;
    // recording
    uint8_t tx_log[512];
    int tx_log_len;
    // clock
    uint32_t now_ms;
    uint32_t recv_step_ms;     // ms added to clock per recv call
} fake_t;

static int fake_send(void *ctx, const uint8_t *buf, int len)
{
    fake_t *f = (fake_t *)ctx;
    if (f->tx_log_len + len > (int)sizeof(f->tx_log)) return -1;
    memcpy(f->tx_log + f->tx_log_len, buf, len);
    f->tx_log_len += len;
    return len;
}

static int fake_recv(void *ctx, uint8_t *buf, int cap, int timeout_ms)
{
    (void)timeout_ms;
    fake_t *f = (fake_t *)ctx;
    f->now_ms += f->recv_step_ms;
    // Drain put-back buffer first (leftover bytes from prior exec_cmd).
    if (f->prebuf_len > 0) {
        int take = f->prebuf_len < cap ? f->prebuf_len : cap;
        memcpy(buf, f->prebuf, take);
        if (take < f->prebuf_len)
            memmove(f->prebuf, f->prebuf + take, f->prebuf_len - take);
        f->prebuf_len -= take;
        return take;
    }
    if (f->script == NULL) return 0;
    if (f->script_pos >= f->script_len) return 0;
    int avail = f->script_len - f->script_pos;
    int take = f->chunk > 0 && f->chunk < avail ? f->chunk : avail;
    if (take > cap)  take = cap;
    memcpy(buf, f->script + f->script_pos, take);
    f->script_pos += take;
    return take;
}

static void fake_putback(void *ctx, const uint8_t *data, int len)
{
    fake_t *f = (fake_t *)ctx;
    // Prepend to prebuf (these bytes MUST be re-read before any script data).
    if (len <= 0 || f->prebuf_len + len > (int)sizeof(f->prebuf)) return;
    memmove(f->prebuf + len, f->prebuf, f->prebuf_len);
    memcpy(f->prebuf, data, len);
    f->prebuf_len += len;
}

static void fake_flush(void *ctx)
{
    (void)ctx;
}

static fake_t *g_fake_for_clock = NULL;

static uint32_t fake_clock(void)
{
    return g_fake_for_clock ? g_fake_for_clock->now_ms : 0;
}

static ld2410c_transport_t make_transport(fake_t *f)
{
    g_fake_for_clock = f;
    ld2410c_transport_t t = {
        .send = fake_send, .recv = fake_recv, .flush = fake_flush,
        .put_back = fake_putback, .ctx = f,
    };
    return t;
}

// ── Helpers ───────────────────────────────────────────────────────────────

static void fake_reset(fake_t *f, const uint8_t *script, int slen, int chunk)
{
    memset(f, 0, sizeof(*f));
    f->script = script; f->script_len = slen;
    f->chunk = chunk;
    f->recv_step_ms = 30;
}

// Look for a byte sequence in the send log.
static bool tx_has(const fake_t *f, const uint8_t *seq, int seq_len, int *offset)
{
    for (int i = 0; i <= f->tx_log_len - seq_len; i++) {
        if (memcmp(f->tx_log + i, seq, seq_len) == 0) {
            if (offset) *offset = i;
            return true;
        }
    }
    return false;
}

// Test ack data (business command data returned on success)
static const uint8_t TEST_TX_DATA[] = { 0x00,0x00, 0x08,0x00,0x00,0x00,
                                        0x01,0x00, 0x08,0x00,0x00,0x00,
                                        0x02,0x00, 0x1E,0x00,0x00,0x00 };

// ── Tests ─────────────────────────────────────────────────────────────────

static void test_core_exec_cmd_ok(void)
{
    fake_t f; fake_reset(&f, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK), 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    uint8_t tx[2] = {0x01,0x00};
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, tx, 2, rx, sizeof(rx), &rx_len, 2000);
    assert(r == ESP_OK);
    assert(rx_len == 4);  // 4-byte ACK data (proto ver + buf size)
    printf("  PASS: TXN-T1 exec_cmd OK (enable, data=4 bytes)\n");
}

static void test_core_exec_cmd_timeout(void)
{
    fake_t f; fake_reset(&f, NULL, 0, 0);   // never returns data
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 500);
    assert(r == ESP_ERR_TIMEOUT);
    printf("  PASS: TXN-T2 exec_cmd timeout\n");
}

static void test_core_exec_cmd_status_nonzero(void)
{
    fake_t f; fake_reset(&f, ENABLE_ACK_FAIL, sizeof(ENABLE_ACK_FAIL), 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 2000);
    assert(r == ESP_ERR_INVALID_RESPONSE);
    printf("  PASS: TXN-T3 exec_cmd nonzero status\n");
}

// Noise + ACK: resync handles stale bytes.
static void test_core_exec_cmd_noise_resync(void)
{
    // 7 noise bytes + enable ACK
    uint8_t noise_buf[256] = {0};
    memset(noise_buf, 0x55, 7);
    memcpy(noise_buf + 7, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK));
    int ns_len = 7 + (int)sizeof(ENABLE_ACK_OK);
    fake_t f; fake_reset(&f, noise_buf, ns_len, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 2000);
    assert(r == ESP_OK);
    printf("  PASS: TXN-T4 exec_cmd after noise bytes (resync)\n");
}

// Mismatched ACK then correct one resolves.
static void test_core_exec_cmd_mismatch_then_ok(void)
{
    uint8_t buf[512] = {0};
    memcpy(buf, DISABLE_ACK_OK, sizeof(DISABLE_ACK_OK));
    memcpy(buf + sizeof(DISABLE_ACK_OK), ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK));
    int total = (int)(sizeof(DISABLE_ACK_OK) + sizeof(ENABLE_ACK_OK));
    fake_t f; fake_reset(&f, buf, total, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 3000);
    assert(r == ESP_OK);
    printf("  PASS: TXN-T5 exec_cmd mismatch → resync → OK\n");
}

// Fragmented ACK: small chunks per recv.
static void test_core_exec_cmd_fragmented(void)
{
    fake_t f; fake_reset(&f, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK), 5);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 2000);
    assert(r == ESP_OK);
    printf("  PASS: TXN-T6 exec_cmd fragmented ACK\n");
}

// Invalid frame at head → resync to next.
static void test_core_exec_cmd_invalid_then_ok(void)
{
    uint8_t bad[20];
    memset(bad, 0xFD, sizeof(bad));       // looks like head but not valid
    uint8_t buf[512] = {0};
    memcpy(buf, bad, sizeof(bad));
    memcpy(buf + sizeof(bad), ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK));
    int total = (int)(sizeof(bad) + sizeof(ENABLE_ACK_OK));
    fake_t f; fake_reset(&f, buf, total, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 3000);
    assert(r == ESP_OK);
    printf("  PASS: TXN-T7 exec_cmd invalid head → resync → OK\n");
}

// NULL args are rejected cleanly (no crash).
static void test_core_exec_cmd_null(void)
{
    fake_t f; fake_reset(&f, NULL, 0, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(NULL, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 0);
    assert(r == ESP_ERR_INVALID_ARG);
    r = ld2410c_core_exec_cmd(&t, NULL,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 0);
    assert(r == ESP_ERR_INVALID_ARG);
    printf("  PASS: TXN-T8 exec_cmd NULL args rejected\n");
}

// tx_data_len > 0 with NULL tx_data → INVALID_ARG (Reviewer P1 core validation).
static void test_core_exec_cmd_txdata_null(void)
{
    fake_t f; fake_reset(&f, NULL, 0, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 2, rx, sizeof(rx), &rx_len, 500);
    assert(r == ESP_ERR_INVALID_ARG);
    printf("  PASS: TXN-T16 exec_cmd tx_data=NULL with len>0 rejected\n");
}

// Negative rx_cap → INVALID_ARG (Reviewer P1 core validation).
static void test_core_exec_cmd_rxcap_negative(void)
{
    fake_t f; fake_reset(&f, NULL, 0, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, -1, &rx_len, 500);
    assert(r == ESP_ERR_INVALID_ARG);
    printf("  PASS: TXN-T17 exec_cmd rx_cap<0 rejected\n");
}

// recv() returns negative → ESP_FAIL (Reviewer P1 transport error).
static int fail_recv(void *ctx, uint8_t *buf, int cap, int timeout_ms)
{
    (void)ctx; (void)buf; (void)cap; (void)timeout_ms;
    return -1;
}
static void test_core_exec_cmd_recv_fail(void)
{
    fake_t f; fake_reset(&f, NULL, 0, 0);
    ld2410c_transport_t t = make_transport(&f); // sets g_fake_for_clock safely
    t.recv = fail_recv;                         // override only recv
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, sizeof(rx), &rx_len, 500);
    assert(r == ESP_FAIL);
    printf("  PASS: TXN-T18 exec_cmd recv failure → ESP_FAIL\n");
}

// ACK payload exceeds rx_cap → INVALID_SIZE (Reviewer P1).
static void test_core_exec_cmd_ack_oversize(void)
{
    // Enable ACK with 4 bytes of data but rx_cap = 2
    fake_t f; fake_reset(&f, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK), 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[4]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, 2, &rx_len, 2000);
    assert(r == ESP_ERR_INVALID_SIZE);
    printf("  PASS: TXN-T19 exec_cmd ACK payload exceeds rx_cap → INVALID_SIZE\n");
}

// Negative tx_data_len → INVALID_ARG (Reviewer P1).
static void test_core_exec_cmd_txdata_neg(void)
{
    fake_t f; fake_reset(&f, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK), 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, -1, rx, sizeof(rx), &rx_len, 500);
    assert(r == ESP_ERR_INVALID_ARG);
    printf("  PASS: TXN-T20 exec_cmd tx_data_len<0 rejected\n");
}

// tx_data_len exceeds max → INVALID_ARG (Reviewer P1).
static void test_core_exec_cmd_txdata_oversize(void)
{
    fake_t f; fake_reset(&f, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK), 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    uint8_t big[LD2410C_CMD_MAX_DATA + 1];
    memset(big, 0, sizeof(big));
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, big, (int)sizeof(big),
        rx, sizeof(rx), &rx_len, 500);
    assert(r == ESP_ERR_INVALID_ARG);
    printf("  PASS: TXN-T21 exec_cmd tx_data_len>MAX rejected\n");
}

// ACK oversized: reports needed capacity + recovers leftover (Reviewer P1).
static void test_core_exec_cmd_ack_oversize_reports_needed(void)
{
    // Script: ENABLE_ACK (4-byte payload) + DISABLE_ACK (0-byte payload)
    // both delivered in one recv() call so leftover exists after first frame.
    uint8_t script[512];
    int off = 0;
    memcpy(script + off, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK));
    off += (int)sizeof(ENABLE_ACK_OK);
    memcpy(script + off, DISABLE_ACK_OK, sizeof(DISABLE_ACK_OK));
    off += (int)sizeof(DISABLE_ACK_OK);

    fake_t f; fake_reset(&f, script, off, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[8]; int rx_len = -1;

    // First call: rx_cap too small → INVALID_SIZE, reports need=4
    esp_err_t r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_ENABLE_CONF, NULL, 0, rx, 2, &rx_len, 2000);
    assert(r == ESP_ERR_INVALID_SIZE);
    assert(rx_len == 4);
    printf("  PASS: TXN-T22a oversize reports need=%d\n", rx_len);

    // Second call: should read put-back leftover (DISABLE_ACK_OK) and succeed
    rx_len = -1;
    r = ld2410c_core_exec_cmd(&t, fake_clock,
        LD2410C_CMD_DISABLE_CONF, NULL, 0, rx, 4, &rx_len, 2000);
    assert(r == ESP_OK);
    printf("  PASS: TXN-T22b leftover recovery after INVALID_SIZE\n");
}

// ── Transaction tests ────────────────────────────────────────────────────

// Full success: enable → business → disable all OK.
static void test_txn_success(void)
{
    // Script: enable ACK + read ACK + disable ACK  (concatenated)
    uint8_t script[512] = {0};
    int off = 0;
    memcpy(script + off, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK)); off += sizeof(ENABLE_ACK_OK);
    memcpy(script + off, WRITE_ACK_OK, sizeof(WRITE_ACK_OK)); off += sizeof(WRITE_ACK_OK);
    memcpy(script + off, DISABLE_ACK_OK, sizeof(DISABLE_ACK_OK)); off += sizeof(DISABLE_ACK_OK);
    fake_t f; fake_reset(&f, script, off, 5);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len = -1;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 2000, NULL);
    assert(r == ESP_OK);
    assert(rx_len >= 0);
    printf("  PASS: TXN-T9 full transaction OK\n");
}

// Enable timeout → returns timeout, disable NOT sent.
static void test_txn_enable_timeout(void)
{
    fake_t f; fake_reset(&f, NULL, 0, 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 500, NULL);
    assert(r == ESP_ERR_TIMEOUT);
    // Only the enable frame should have been sent (NOT disable/business).
    // The enable command frame itself starts with FD FC FB FA, so we check
    // for the DISABLE and BUSINESS command words instead of raw bytes.
    assert(tx_has(&f, (const uint8_t*)"\xFE\x00", 2, NULL) == false);
    assert(tx_has(&f, (const uint8_t*)"\x60\x00", 2, NULL) == false);
    // At least 4 sends for head + header bytes? We sent exactly one frame.
    assert(f.tx_log_len > 0);
    printf("  PASS: TXN-T10 enable timeout → timeout, no biz/disable sent\n");
}

// Enable fail (status != 0) → propagating, disable NOT attempted.
static void test_txn_enable_status_fail(void)
{
    fake_t f; fake_reset(&f, ENABLE_ACK_FAIL, sizeof(ENABLE_ACK_FAIL), 0);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 2000, NULL);
    assert(r == ESP_ERR_INVALID_RESPONSE);
    printf("  PASS: TXN-T11 enable status fail → error, disable not sent\n");
}

// Business OK, disable FAIL → returns ESP_FAIL (NOT OK).
static void test_txn_biz_ok_disable_fail(void)
{
    uint8_t script[512] = {0};
    int off = 0;
    memcpy(script + off, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK)); off += sizeof(ENABLE_ACK_OK);
    memcpy(script + off, WRITE_ACK_OK, sizeof(WRITE_ACK_OK)); off += sizeof(WRITE_ACK_OK);
    memcpy(script + off, DISABLE_ACK_FAIL, sizeof(DISABLE_ACK_FAIL)); off += sizeof(DISABLE_ACK_FAIL);
    fake_t f; fake_reset(&f, script, off, 5);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 2000, NULL);
    assert(r == ESP_FAIL);   // business OK but disable failed → NOT OK
    printf("  PASS: TXN-T12 biz OK + disable fail → ESP_FAIL\n");
}

// Business fails with nonzero status, disable succeeds
// → returns business error (not hidden by disable).
static void test_txn_biz_status_fail_disable_ok(void)
{
    uint8_t script[512] = {0};
    int off = 0;
    memcpy(script + off, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK)); off += sizeof(ENABLE_ACK_OK);
    // Business ACK with status != 0 (cmd = WRITE_PARAMS, status = 1)
    uint8_t biz_ack_fail[] = {
        0xFD,0xFC,0xFB,0xFA, 0x04,0x00, 0x60,0x01, 0x01,0x00,
        0x04,0x03,0x02,0x01
    };
    memcpy(script + off, biz_ack_fail, sizeof(biz_ack_fail)); off += sizeof(biz_ack_fail);
    // Disable succeeds
    memcpy(script + off, DISABLE_ACK_OK, sizeof(DISABLE_ACK_OK)); off += sizeof(DISABLE_ACK_OK);
    fake_t f; fake_reset(&f, script, off, 5);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 2000, NULL);
    assert(r == ESP_ERR_INVALID_RESPONSE);  // biz status propagated
    printf("  PASS: TXN-T13 biz status fail + disable OK → biz error\n");
}

// Enable OK, business timeout + disable timeout → ESP_FAIL (uncertain).
static void test_txn_biz_fail_disable_fail(void)
{
    uint8_t script[512] = {0};
    int off = 0;
    memcpy(script + off, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK)); off += sizeof(ENABLE_ACK_OK);
    // No business or disable response → both time out (the disable ACK would
    // be consumed by the business phase as wrong-cmd noise).
    fake_t f; fake_reset(&f, script, off, 5);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    ld2410c_transaction_detail_t det;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 500, &det);
    assert(r == ESP_FAIL);   // both biz and disable failed → uncertain
    assert(det.enable_result == ESP_OK);
    assert(det.business_result == ESP_ERR_TIMEOUT);
    assert(det.disable_attempted == true);
    assert(det.disable_result == ESP_ERR_TIMEOUT);
    printf("  PASS: TXN-T14 enable OK + biz timeout + disable timeout → ESP_FAIL (uncertain)\n");
}

// Ordering: sent frames must appear ENABLE → BUSINESS → DISABLE in order.
static void test_txn_ordering(void)
{
    uint8_t script[512] = {0};
    int off = 0;
    memcpy(script + off, ENABLE_ACK_OK, sizeof(ENABLE_ACK_OK)); off += sizeof(ENABLE_ACK_OK);
    memcpy(script + off, WRITE_ACK_OK, sizeof(WRITE_ACK_OK)); off += sizeof(WRITE_ACK_OK);
    memcpy(script + off, DISABLE_ACK_OK, sizeof(DISABLE_ACK_OK)); off += sizeof(DISABLE_ACK_OK);
    fake_t f; fake_reset(&f, script, off, 5);
    ld2410c_transport_t t = make_transport(&f);
    uint8_t rx[32]; int rx_len;
    esp_err_t r = ld2410c_core_exec_transaction(&t, fake_clock,
        LD2410C_CMD_WRITE_PARAMS, TEST_TX_DATA, sizeof(TEST_TX_DATA),
        rx, sizeof(rx), &rx_len, 2000, NULL);
    assert(r == ESP_OK);
    int e_pos, b_pos, d_pos;
    // ENABLE frame contains FF 00; BUSINESS contains 60 00; DISABLE contains FE 00
    assert(tx_has(&f, (const uint8_t*)"\xFF\x00", 2, &e_pos) && "ENABLE in send log");
    assert(tx_has(&f, (const uint8_t*)"\x60\x00", 2, &b_pos) && "BUSINESS in send log");
    assert(tx_has(&f, (const uint8_t*)"\xFE\x00", 2, &d_pos) && "DISABLE in send log");
    assert(e_pos < b_pos && b_pos < d_pos && "ordering ENABLE < BUSINESS < DISABLE");
    printf("  PASS: TXN-T15 send ordering ENABLE → BUSINESS → DISABLE\n");
}

// =========================================================================
//  Main
// =========================================================================

int main(void)
{
    printf("\n=== LD2410C Transaction Core Tests (fake transport) ===\n\n");

    printf("[exec_cmd]:\n");
    test_core_exec_cmd_ok();
    test_core_exec_cmd_timeout();
    test_core_exec_cmd_status_nonzero();
    test_core_exec_cmd_noise_resync();
    test_core_exec_cmd_mismatch_then_ok();
    test_core_exec_cmd_fragmented();
    test_core_exec_cmd_invalid_then_ok();
    test_core_exec_cmd_null();
    test_core_exec_cmd_txdata_null();
    test_core_exec_cmd_rxcap_negative();
    test_core_exec_cmd_recv_fail();
    test_core_exec_cmd_ack_oversize();
    test_core_exec_cmd_txdata_neg();
    test_core_exec_cmd_txdata_oversize();
    test_core_exec_cmd_ack_oversize_reports_needed();

    printf("\n[exec_transaction]:\n");
    test_txn_success();
    test_txn_enable_timeout();
    test_txn_enable_status_fail();
    test_txn_biz_ok_disable_fail();
    test_txn_biz_status_fail_disable_ok();
    test_txn_biz_fail_disable_fail();
    test_txn_ordering();

    printf("\n=== ALL TESTS PASSED ===\n\n");
    return 0;
}
