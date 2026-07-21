// PrivacySense Matter Room Hub - LD2410C parser unit tests
//
// Tests R03 (normal-mode frame parsing), R04 (protocol robustness), and
// R11 (command-mode frame building/validation per V1.09 official spec).
// Uses the REAL parser functions from ld2410c_parser.h / ld2410c_parser.c.

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#include "../ld2410c_parser.h"

// =========================================================================
//  Test helper: build a valid V1.09 normal-mode frame
// =========================================================================
static void build_frame(uint8_t *out, uint8_t state,
                        uint16_t moving_dist, uint8_t moving_eng,
                        uint16_t static_dist, uint8_t static_eng,
                        uint16_t detect_dist)
{
    out[0] = 0xF4; out[1] = 0xF3; out[2] = 0xF2; out[3] = 0xF1;
    out[4] = 0x0D; out[5] = 0x00;
    out[6] = 0x02;
    out[7] = 0xAA;
    out[8] = state;
    out[9]  = (uint8_t)(moving_dist & 0xFF);
    out[10] = (uint8_t)(moving_dist >> 8);
    out[11] = moving_eng;
    out[12] = (uint8_t)(static_dist & 0xFF);
    out[13] = (uint8_t)(static_dist >> 8);
    out[14] = static_eng;
    out[15] = (uint8_t)(detect_dist & 0xFF);
    out[16] = (uint8_t)(detect_dist >> 8);
    out[17] = 0x55; out[18] = 0x00;
    out[19] = 0xF8; out[20] = 0xF7; out[21] = 0xF6; out[22] = 0xF5;
}

// Build a simulated ACK frame matching the V1.09 spec.
// cmd_word: original command (ACK will have |0x0100 applied internally).
// status:   LE uint16 ACK status.
// data:     payload bytes after status.
// Returns total frame size.
static int build_ack_frame(uint8_t *out, int cap,
                           uint16_t cmd_word, uint16_t status,
                           const uint8_t *data, int data_len)
{
    (void)cap;
    uint16_t ack_cmd = cmd_word | LD2410C_CMD_ACK_MASK;
    uint16_t data_field_len = LD2410C_CMD_WORD_LEN + LD2410C_CMD_STATUS_LEN
                              + data_len;
    int total = LD2410C_CMD_OVERHEAD + (int)data_field_len;

    out[0] = 0xFD; out[1] = 0xFC; out[2] = 0xFB; out[3] = 0xFA;
    out[4] = (uint8_t)(data_field_len & 0xFF);
    out[5] = (uint8_t)(data_field_len >> 8);
    out[6] = (uint8_t)(ack_cmd & 0xFF);
    out[7] = (uint8_t)(ack_cmd >> 8);
    out[8] = (uint8_t)(status & 0xFF);
    out[9] = (uint8_t)(status >> 8);
    for (int i = 0; i < data_len; i++) {
        out[10 + i] = data[i];
    }
    int tail_off = total - 4;
    out[tail_off]     = 0x04;
    out[tail_off + 1] = 0x03;
    out[tail_off + 2] = 0x02;
    out[tail_off + 3] = 0x01;
    return total;
}

// =========================================================================
//  R03: Frame parsing — 4 target states
// =========================================================================

static void test_R03_state_none(void)
{
    uint8_t frame[23];
    build_frame(frame, 0x00, 0, 0, 0, 0, 0);
    ld2410c_radar_data_t out = {0};
    bool ok = ld2410c_try_parse_frame(frame, sizeof(frame), &out);
    assert(ok == true);
    assert(out.valid == true);
    assert(out.target_present == false);
    assert(out.moving_distance_cm == 0);
    assert(out.static_distance_cm == 0);
    assert(out.moving_energy == 0);
    assert(out.static_energy == 0);
    printf("  PASS: R03-T1 state=0x00 (no target)\n");
}

static void test_R03_state_moving(void) { /* unchanged from prior */
    uint8_t frame[23];
    build_frame(frame, 0x01, 150, 45, 0, 0, 150);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == true);
    assert(out.moving_distance_cm == 150 && out.moving_energy == 45);
    printf("  PASS: R03-T2 state=0x01 (moving, 150cm@45)\n");
}
static void test_R03_state_static(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x02, 0, 0, 300, 60, 300);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == true);
    assert(out.static_distance_cm == 300 && out.static_energy == 60);
    printf("  PASS: R03-T3 state=0x02 (static, 300cm@60)\n");
}
static void test_R03_state_both(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x03, 100, 30, 250, 55, 250);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == true);
    assert(out.moving_distance_cm == 100 && out.static_distance_cm == 250);
    printf("  PASS: R03-T4 state=0x03 (both)\n");
}
static void test_R03_max_values(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x03, 600, 100, 600, 100, 600);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == true);
    assert(out.moving_distance_cm == 600 && out.moving_energy == 100);
    printf("  PASS: R03-T5 max values\n");
}
static void test_R03_min_values(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 0, 0, 0, 0, 0);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == true);
    printf("  PASS: R03-T6 min values\n");
}

// =========================================================================
//  R04: Protocol robustness — bad frames, noise, recovery
// =========================================================================

static void test_R04_no_head(void) { /* unchanged */
    uint8_t garbage[23];
    memset(garbage, 0xAA, sizeof(garbage));
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(garbage, sizeof(garbage), &out) == false);
    printf("  PASS: R04-T1 garbage data -> parse fails\n");
}
static void test_R04_bad_length_small(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    frame[4] = 0x05; frame[5] = 0x00;
    ld2410c_radar_data_t o = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &o) == false);
    assert(ld2410c_frame_total_size(frame, sizeof(frame)) == -1);
    printf("  PASS: R04-T2 length=5 (<13)\n");
}
static void test_R04_incomplete_frame(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    assert(ld2410c_frame_total_size(frame, 15) == 0);
    printf("  PASS: R04-T3 incomplete (15/23)\n");
}
static void test_R04_truncated_head(void) { /* unchanged */
    uint8_t frame[4] = {0xF4, 0xF3, 0xF2, 0x00};
    assert(ld2410c_is_head(frame) == false);
    printf("  PASS: R04-T4 truncated head\n");
}
static void test_R04_bad_tail_verify(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    frame[17] = 0xFF; frame[18] = 0xFF;
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T5 corrupt tail verify\n");
}
static void test_R04_bad_tail_eof(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    frame[19] = 0x00; frame[20] = 0x00; frame[21] = 0x00; frame[22] = 0x00;
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T6 corrupt end-of-frame\n");
}
static void test_R04_invalid_state(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x04, 100, 50, 200, 40, 200);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T7 state=0x04 (>3)\n");
}
static void test_R04_energy_overflow(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 101, 200, 40, 200);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T8 energy=101 (>100)\n");
}
static void test_R04_distance_overflow(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 601, 50, 200, 40, 200);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T9 dist=601 (>600)\n");
}
static void test_R04_engineering_mode(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    frame[6] = 0x01;
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T10 engineering mode\n");
}
static void test_R04_noise_before_frame(void) { /* unchanged */
    uint8_t buf[30];
    memset(buf, 0x55, 7);
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    memcpy(buf + 7, frame, 23);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(buf, 30, &out) == false);
    assert(ld2410c_try_parse_frame(buf + 7, 23, &out) == true);
    printf("  PASS: R04-T11 noise before frame\n");
}
static void test_R04_back_to_back(void) { /* unchanged */
    uint8_t buf[46];
    build_frame(buf, 0x01, 100, 50, 200, 40, 200);
    build_frame(buf + 23, 0x02, 0, 0, 300, 60, 300);
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(buf, 46, &out) == true);
    assert(out.moving_distance_cm == 100);
    assert(ld2410c_try_parse_frame(buf + 23, 23, &out) == true);
    assert(out.static_distance_cm == 300);
    printf("  PASS: R04-T12 two back-to-back frames\n");
}
static void test_R04_empty_buffer(void) { /* unchanged */
    uint8_t buf[1] = {0};
    assert(ld2410c_frame_total_size(buf, 0) == 0);
    printf("  PASS: R04-T13 empty buffer\n");
}
static void test_R04_head_only(void) { /* unchanged */
    uint8_t buf[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    assert(ld2410c_frame_total_size(buf, 4) == 0);
    printf("  PASS: R04-T14 head only\n");
}
static void test_R04_wrong_data_type(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    frame[6] = 0xFF;
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T15 wrong data_type (0xFF)\n");
}
static void test_R04_wrong_marker(void) { /* unchanged */
    uint8_t frame[23];
    build_frame(frame, 0x01, 100, 50, 200, 40, 200);
    frame[7] = 0xBB;
    ld2410c_radar_data_t out = {0};
    assert(ld2410c_try_parse_frame(frame, sizeof(frame), &out) == false);
    printf("  PASS: R04-T16 wrong AA marker (0xBB)\n");
}

// =========================================================================
//  R11: Command-mode frame building/validation (V1.09 spec)
// =========================================================================

// V1.09 golden vectors from official spec:
//   Enable config: FD FC FB FA 04 00 FF 00 01 00 04 03 02 01
//   Enable ACK:    FD FC FB FA 08 00 FF 01 00 00 01 00 40 00 04 03 02 01
//   Disable:       FD FC FB FA 02 00 FE 00 04 03 02 01
//   Read params:   FD FC FB FA 02 00 61 00 04 03 02 01

static const uint8_t GOLDEN_ENABLE_CMD[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00,
    0x04, 0x03, 0x02, 0x01
};
static const uint8_t GOLDEN_ENABLE_ACK[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xFF, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x40, 0x00,
    0x04, 0x03, 0x02, 0x01
};
static const uint8_t GOLDEN_DISABLE_CMD[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00,
    0x04, 0x03, 0x02, 0x01
};
static const uint8_t GOLDEN_READ_CMD[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x61, 0x00,
    0x04, 0x03, 0x02, 0x01
};

static void test_R11_enable_cmd_frame(void)
{
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    uint8_t data[] = { 0x01, 0x00 };  // enabling value = 0x0001 LE
    uint8_t *ret = ld2410c_build_cmd_frame(frame, sizeof(frame), &len,
                                            LD2410C_CMD_ENABLE_CONF,
                                            data, sizeof(data));
    assert(ret != NULL);
    assert(len == (int)sizeof(GOLDEN_ENABLE_CMD));
    assert(memcmp(frame, GOLDEN_ENABLE_CMD, len) == 0);
    assert(ld2410c_cmd_frame_valid(frame, len) == true);
    printf("  PASS: R11-T1 golden enable-cmd frame\n");
}

static void test_R11_enable_ack_parse(void)
{
    int total = (int)sizeof(GOLDEN_ENABLE_ACK);
    assert(ld2410c_cmd_frame_valid(GOLDEN_ENABLE_ACK, total) == true);
    assert(ld2410c_cmd_ack_matches(GOLDEN_ENABLE_ACK, total,
                                    LD2410C_CMD_ENABLE_CONF) == true);
    // Extract status @ [8-9]
    uint16_t status = (uint16_t)GOLDEN_ENABLE_ACK[8]
                      | ((uint16_t)GOLDEN_ENABLE_ACK[9] << 8);
    assert(status == 0);
    // Extract data: protocol version @ [10-11] = 0x0001, buffer @ [12-13] = 0x0040
    uint16_t ver = (uint16_t)GOLDEN_ENABLE_ACK[10]
                   | ((uint16_t)GOLDEN_ENABLE_ACK[11] << 8);
    uint16_t buf = (uint16_t)GOLDEN_ENABLE_ACK[12]
                   | ((uint16_t)GOLDEN_ENABLE_ACK[13] << 8);
    assert(ver == 1);
    // ACK data len = length - cmd_word(2) - status(2) = 8 - 2 - 2 = 4
    uint16_t length = (uint16_t)GOLDEN_ENABLE_ACK[4]
                      | ((uint16_t)GOLDEN_ENABLE_ACK[5] << 8);
    int ack_data_len = (int)length - LD2410C_CMD_WORD_LEN
                       - LD2410C_CMD_STATUS_LEN;
    assert(ack_data_len == 4);
    printf("  PASS: R11-T2 golden enable-ack parse (ver=%u, buf=%u)\n",
           (unsigned)ver, (unsigned)buf);
}

static void test_R11_disable_cmd_frame(void)
{
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    uint8_t *ret = ld2410c_build_cmd_frame(frame, sizeof(frame), &len,
                                            LD2410C_CMD_DISABLE_CONF,
                                            NULL, 0);
    assert(ret != NULL);
    assert(len == (int)sizeof(GOLDEN_DISABLE_CMD));
    assert(memcmp(frame, GOLDEN_DISABLE_CMD, len) == 0);
    assert(ld2410c_cmd_frame_valid(frame, len) == true);
    printf("  PASS: R11-T3 golden disable-cmd frame\n");
}

static void test_R11_read_params_cmd_frame(void)
{
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    uint8_t *ret = ld2410c_build_cmd_frame(frame, sizeof(frame), &len,
                                            LD2410C_CMD_READ_PARAMS,
                                            NULL, 0);
    assert(ret != NULL);
    assert(len == (int)sizeof(GOLDEN_READ_CMD));
    assert(memcmp(frame, GOLDEN_READ_CMD, len) == 0);
    assert(ld2410c_cmd_frame_valid(frame, len) == true);
    printf("  PASS: R11-T4 golden read-params frame\n");
}

static void test_R11_ack_mismatch(void)
{
    // Enable ACK should NOT match the disable command
    assert(ld2410c_cmd_ack_matches(GOLDEN_ENABLE_ACK,
                                    (int)sizeof(GOLDEN_ENABLE_ACK),
                                    LD2410C_CMD_DISABLE_CONF) == false);
    // Nor match read params
    assert(ld2410c_cmd_ack_matches(GOLDEN_ENABLE_ACK,
                                    (int)sizeof(GOLDEN_ENABLE_ACK),
                                    LD2410C_CMD_READ_PARAMS) == false);
    printf("  PASS: R11-T5 ACK cmd mismatch detection\n");
}

static void test_R11_invalid_head(void)
{
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    uint8_t data[] = { 0x01, 0x00 };
    ld2410c_build_cmd_frame(frame, sizeof(frame), &len,
                             LD2410C_CMD_ENABLE_CONF, data, sizeof(data));
    frame[0] = 0x00;
    assert(ld2410c_cmd_frame_valid(frame, len) == false);
    assert(ld2410c_is_cmd_head(frame) == false);
    printf("  PASS: R11-T6 invalid head -> rejected\n");
}

static void test_R11_corrupt_tail(void)
{
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    uint8_t data[] = { 0x01, 0x00 };
    ld2410c_build_cmd_frame(frame, sizeof(frame), &len,
                             LD2410C_CMD_ENABLE_CONF, data, sizeof(data));
    frame[len - 1] = 0x00;
    assert(ld2410c_cmd_frame_valid(frame, len) == false);
    printf("  PASS: R11-T7 corrupt tail -> rejected\n");
}

static void test_R11_incomplete_frame(void)
{
    // Only first 5 bytes of enable command (head + partial length)
    int sz = ld2410c_cmd_frame_size(GOLDEN_ENABLE_CMD, 5);
    assert(sz == 0);  // NEED_MORE
    printf("  PASS: R11-T8 incomplete frame -> NEED_MORE\n");
}

static void test_R11_write_params_frame(void)
{
    // 0x0060 write with full three-parameter payload (golden vector from
    // official V1.09 spec §2.2.3): motion gate=8, static gate=8, delay=30s
    uint8_t param_data[] = { 0x00, 0x00,   // param word = 0x0000 (motion gate)
                             0x08, 0x00, 0x00, 0x00,  // value = 8 (LE32)
                             0x01, 0x00,   // param word = 0x0001 (static gate)
                             0x08, 0x00, 0x00, 0x00,  // value = 8 (LE32)
                             0x02, 0x00,   // param word = 0x0002 (unoccupied delay)
                             0x1E, 0x00, 0x00, 0x00 };  // value = 30 (LE32)
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    uint8_t *ret = ld2410c_build_cmd_frame(frame, sizeof(frame), &len,
                                            LD2410C_CMD_WRITE_PARAMS,
                                            param_data, sizeof(param_data));
    assert(ret != NULL);
    assert(ld2410c_cmd_frame_valid(frame, len) == true);
    // Verify structure
    uint16_t data_field_len = (uint16_t)frame[4] | ((uint16_t)frame[5] << 8);
    assert(data_field_len ==
           LD2410C_CMD_WORD_LEN + sizeof(param_data));  // 2 + 18 = 20
    uint16_t cmd = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
    assert(cmd == LD2410C_CMD_WRITE_PARAMS);
    // Verify tail
    assert(ld2410c_is_cmd_tail(frame + len - 4));
    printf("  PASS: R11-T9 golden write-params frame (three-param)\n");
}

static void test_R11_ack_with_status(void)
{
    // Build a failing ACK manually and verify status extraction
    uint8_t ack[20];
    int total = build_ack_frame(ack, sizeof(ack),
                                 LD2410C_CMD_ENABLE_CONF,
                                 1,   // status = 1 (failure)
                                 NULL, 0);
    assert(ld2410c_cmd_frame_valid(ack, total) == true);
    assert(ld2410c_cmd_ack_matches(ack, total,
                                    LD2410C_CMD_ENABLE_CONF) == true);
    uint16_t status = (uint16_t)ack[8] | ((uint16_t)ack[9] << 8);
    assert(status == 1);
    printf("  PASS: R11-T10 ACK with non-zero status\n");
}

// ── New R11 extensions: V1.09 golden vectors + validation + resync ─────

static const uint8_t GOLDEN_WRITE_BASIC_5S[] = {
    0xFD,0xFC,0xFB,0xFA, 0x14,0x00, 0x60,0x00,
    0x00,0x00, 0x08,0x00,0x00,0x00,
    0x01,0x00, 0x08,0x00,0x00,0x00,
    0x02,0x00, 0x05,0x00,0x00,0x00,
    0x04,0x03,0x02,0x01
};
static const uint8_t GOLDEN_WRITE_BASIC_30S[] = {
    0xFD,0xFC,0xFB,0xFA, 0x14,0x00, 0x60,0x00,
    0x00,0x00, 0x08,0x00,0x00,0x00,
    0x01,0x00, 0x08,0x00,0x00,0x00,
    0x02,0x00, 0x1E,0x00,0x00,0x00,
    0x04,0x03,0x02,0x01
};
static const uint8_t GOLDEN_SENS_GATE3[] = {
    0xFD,0xFC,0xFB,0xFA, 0x14,0x00, 0x64,0x00,
    0x00,0x00, 0x03,0x00,0x00,0x00,
    0x01,0x00, 0x28,0x00,0x00,0x00,
    0x02,0x00, 0x28,0x00,0x00,0x00,
    0x04,0x03,0x02,0x01
};
static const uint8_t GOLDEN_SENS_ALL[] = {
    0xFD,0xFC,0xFB,0xFA, 0x14,0x00, 0x64,0x00,
    0x00,0x00, 0xFF,0xFF,0x00,0x00,
    0x01,0x00, 0x28,0x00,0x00,0x00,
    0x02,0x00, 0x28,0x00,0x00,0x00,
    0x04,0x03,0x02,0x01
};
// Full 0x0061 read-params ACK, 38 bytes (PDF V1.09 page 9):
//   max gate 8, moving gate 8, static gate 8,
//   gate 0..8 moving sens=20, gate 0..8 static sens=25, unoccupied=5s
static const uint8_t GOLDEN_READ_ACK_38[] = {
    0xFD,0xFC,0xFB,0xFA, 0x1C,0x00, 0x61,0x01, 0x00,0x00,
    0xAA, 0x08, 0x08, 0x08,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,
    0x05,0x00,
    0x04,0x03,0x02,0x01
};
// Payload portion (24 bytes after head/cmd/status)
static const uint8_t GOLDEN_READ_PAYLOAD[] = {
    0xAA, 0x08, 0x08, 0x08,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,
    0x05,0x00
};

static void test_R11_write_basic_5s(void)
{
    ld2410c_basic_params_t p = { .max_moving_gate = 8, .max_static_gate = 8,
                                  .unoccupied_delay_s = 5 };
    uint8_t frame[64]; int len;
    uint8_t *r = ld2410c_build_write_basic_params(frame, sizeof(frame), &len, &p);
    assert(r != NULL);
    assert(len == (int)sizeof(GOLDEN_WRITE_BASIC_5S));
    assert(memcmp(frame, GOLDEN_WRITE_BASIC_5S, len) == 0);
    assert(ld2410c_cmd_frame_valid(frame, len) == true);
    printf("  PASS: R11-T11 golden 0x0060 (gates=8, unocc=5s)\n");
}

static void test_R11_write_basic_30s(void)
{
    ld2410c_basic_params_t p = { .max_moving_gate = 8, .max_static_gate = 8,
                                  .unoccupied_delay_s = 30 };
    uint8_t frame[64]; int len;
    uint8_t *r = ld2410c_build_write_basic_params(frame, sizeof(frame), &len, &p);
    assert(r != NULL);
    assert(len == (int)sizeof(GOLDEN_WRITE_BASIC_30S));
    assert(memcmp(frame, GOLDEN_WRITE_BASIC_30S, len) == 0);
    printf("  PASS: R11-T12 golden 0x0060 (gates=8, unocc=30s)\n");
}

static void test_R11_set_sens_gate3(void)
{
    uint8_t frame[64]; int len;
    uint8_t *r = ld2410c_build_set_sensitivity(frame, sizeof(frame), &len, 3, 40, 40);
    assert(r != NULL);
    assert(len == (int)sizeof(GOLDEN_SENS_GATE3));
    assert(memcmp(frame, GOLDEN_SENS_GATE3, len) == 0);
    assert(ld2410c_cmd_frame_valid(frame, len) == true);
    printf("  PASS: R11-T13 golden 0x0064 (gate=3, sens=40/40)\n");
}

static void test_R11_set_sens_all(void)
{
    uint8_t frame[64]; int len;
    uint8_t *r = ld2410c_build_set_sensitivity(frame, sizeof(frame), &len,
                                                LD2410C_GATE_ALL, 40, 40);
    assert(r != NULL);
    assert(len == (int)sizeof(GOLDEN_SENS_ALL));
    assert(memcmp(frame, GOLDEN_SENS_ALL, len) == 0);
    printf("  PASS: R11-T14 golden 0x0064 (all gates, sens=40/40)\n");
}

static void test_R11_read_ack_38(void)
{
    int total = (int)sizeof(GOLDEN_READ_ACK_38);
    ld2410c_read_params_t out = {0};
    bool ok = ld2410c_parse_read_params_ack(GOLDEN_READ_ACK_38, total, &out);
    assert(ok == true);
    assert(out.max_gate == 8);
    assert(out.max_moving_gate == 8);
    assert(out.max_static_gate == 8);
    for (int g = 0; g <= 8; g++) assert(out.moving_sens[g] == 20);
    for (int g = 0; g <= 8; g++) assert(out.static_sens[g] == 25);
    assert(out.unoccupied_delay_s == 5);
    printf("  PASS: R11-T15 0x0061 ACK 38-byte parse (18 sens values)\n");
}

static void test_R11_read_payload_parse(void)
{
    ld2410c_read_params_t out = {0};
    bool ok = ld2410c_parse_read_params_payload(GOLDEN_READ_PAYLOAD,
                                                 (int)sizeof(GOLDEN_READ_PAYLOAD),
                                                 &out);
    assert(ok == true);
    assert(out.max_gate == 8);
    assert(out.max_moving_gate == 8);
    assert(out.max_static_gate == 8);
    assert(out.unoccupied_delay_s == 5);
    printf("  PASS: R11-T16 0x0061 payload parse\n");
}

static void test_R11_basic_params_reject_gate1(void)
{
    ld2410c_basic_params_t p = { .max_moving_gate = 1, .max_static_gate = 8,
                                  .unoccupied_delay_s = 5 };
    assert(ld2410c_basic_params_valid(&p) == false);
    printf("  PASS: R11-T17 basic params reject gate=1 (<min)\n");
}

static void test_R11_basic_params_reject_gate9(void)
{
    ld2410c_basic_params_t p = { .max_moving_gate = 9, .max_static_gate = 8,
                                  .unoccupied_delay_s = 5 };
    assert(ld2410c_basic_params_valid(&p) == false);
    uint8_t frame[64]; int len;
    assert(ld2410c_build_write_basic_params(frame, sizeof(frame), &len, &p) == NULL);
    printf("  PASS: R11-T18 basic params reject gate=9 (>max)\n");
}

static void test_R11_sensitivity_reject_101(void)
{
    assert(ld2410c_sensitivity_valid(3, 101, 40) == false);
    assert(ld2410c_sensitivity_valid(3, 40, 101) == false);
    uint8_t frame[64]; int len;
    assert(ld2410c_build_set_sensitivity(frame, sizeof(frame), &len, 3, 101, 40) == NULL);
    printf("  PASS: R11-T19 sensitivity reject 101\n");
}

static void test_R11_find_head_noise(void)
{
    uint8_t buf[40];
    memset(buf, 0x55, 7);
    buf[7]=0xFD; buf[8]=0xFC; buf[9]=0xFB; buf[10]=0xFA;
    int off = ld2410c_find_cmd_head(buf, 40);
    assert(off == 7);
    printf("  PASS: R11-T20 find head after 7 noise bytes\n");
}

static void test_R11_find_head_partial(void)
{
    uint8_t buf[4] = {0xFD, 0xFC, 0xFB, 0x00};
    assert(ld2410c_find_cmd_head(buf, 4) == -1);
    printf("  PASS: R11-T21 find head with partial match\n");
}

static void test_R11_read_ack_null(void)
{
    assert(ld2410c_parse_read_params_ack(NULL, 0, NULL) == false);
    printf("  PASS: R11-T22 read-ack parse NULL args\n");
}

static void test_R11_read_ack_status_nonzero(void)
{
    uint8_t bad[40];
    memcpy(bad, GOLDEN_READ_ACK_38, sizeof(GOLDEN_READ_ACK_38));
    bad[8] = 0x01; bad[9] = 0x00;
    ld2410c_read_params_t out;
    assert(ld2410c_parse_read_params_ack(bad, sizeof(GOLDEN_READ_ACK_38), &out) == false);
    printf("  PASS: R11-T23 read-ack parse nonzero status rejected\n");
}

static void test_R11_payload_short(void)
{
    uint8_t shortbuf[3] = {0xAA, 0x08, 0x08};
    ld2410c_read_params_t out;
    assert(ld2410c_parse_read_params_payload(shortbuf, 3, &out) == false);
    printf("  PASS: R11-T24 payload parse short data rejected\n");
}

// =========================================================================
//  Main test runner
// =========================================================================

void test_main(void)
{
    printf("\n=== LD2410C Parser Unit Tests (production parser) ===\n\n");

    printf("[R03] Frame parsing -- 4 target states:\n");
    test_R03_state_none();
    test_R03_state_moving();
    test_R03_state_static();
    test_R03_state_both();
    test_R03_max_values();
    test_R03_min_values();

    printf("\n[R04] Protocol robustness:\n");
    test_R04_no_head();
    test_R04_bad_length_small();
    test_R04_incomplete_frame();
    test_R04_truncated_head();
    test_R04_bad_tail_verify();
    test_R04_bad_tail_eof();
    test_R04_invalid_state();
    test_R04_energy_overflow();
    test_R04_distance_overflow();
    test_R04_engineering_mode();
    test_R04_noise_before_frame();
    test_R04_back_to_back();
    test_R04_empty_buffer();
    test_R04_head_only();
    test_R04_wrong_data_type();
    test_R04_wrong_marker();

    printf("\n[R11] Command-mode (V1.09 spec golden vectors):\n");
    test_R11_enable_cmd_frame();
    test_R11_enable_ack_parse();
    test_R11_disable_cmd_frame();
    test_R11_read_params_cmd_frame();
    test_R11_ack_mismatch();
    test_R11_invalid_head();
    test_R11_corrupt_tail();
    test_R11_incomplete_frame();
    test_R11_write_params_frame();
    test_R11_ack_with_status();
    test_R11_write_basic_5s();
    test_R11_write_basic_30s();
    test_R11_set_sens_gate3();
    test_R11_set_sens_all();
    test_R11_read_ack_38();
    test_R11_read_payload_parse();
    test_R11_basic_params_reject_gate1();
    test_R11_basic_params_reject_gate9();
    test_R11_sensitivity_reject_101();
    test_R11_find_head_noise();
    test_R11_find_head_partial();
    test_R11_read_ack_null();
    test_R11_read_ack_status_nonzero();
    test_R11_payload_short();

    printf("\n=== ALL TESTS PASSED ===\n\n");
}

int main(void)
{
    test_main();
    return 0;
}
