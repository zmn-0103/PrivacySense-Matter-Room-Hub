// PrivacySense Matter Room Hub - LD2410C V1.09 frame parser implementation
//
// Implements both normal-mode reporting frame parsing and command-mode frame
// building/parsing. See ld2410c_parser.h for protocol reference.
//
// All functions are self-contained (no ESP-IDF dependencies) so they can be
// compiled by the host-side unit test.

#include "ld2410c_parser.h"

// ── Normal-mode frame helpers ──────────────────────────────────────────────

int ld2410c_frame_total_size(const uint8_t *buf, int avail)
{
    if (avail < LD2410C_FRAME_HEAD_LEN + LD2410C_FRAME_LEN_FIELD) return 0;
    uint16_t intra_len = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    if (intra_len < LD2410C_FRAME_MIN_LEN) return -1;
    int total = LD2410C_FRAME_HEAD_LEN + LD2410C_FRAME_LEN_FIELD +
                (int)intra_len + LD2410C_FRAME_TAIL_LEN;
    if (total > LD2410C_RX_BUF_CAP) return -1;
    if (avail < total) return 0;
    return total;
}

bool ld2410c_try_parse_frame(const uint8_t *buf, int avail,
                             ld2410c_radar_data_t *out)
{
    int total = ld2410c_frame_total_size(buf, avail);
    if (total <= 0)    return false;
    if (avail < total) return false;
    if (!ld2410c_is_head(buf)) return false;

    const uint8_t *in = buf + LD2410C_FRAME_HEAD_LEN + LD2410C_FRAME_LEN_FIELD;
    uint16_t intra_len = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);

    if (in[0] != LD2410C_DATA_TYPE_NORMAL) return false;
    if (in[1] != LD2410C_AA_MARKER)        return false;
    if (in[intra_len - 2] != LD2410C_TAIL_VERIFY1) return false;
    if (in[intra_len - 1] != LD2410C_TAIL_VERIFY2) return false;
    if (!ld2410c_is_tail(buf + total - LD2410C_FRAME_TAIL_LEN)) return false;

    uint8_t state = in[2];
    if (state > LD2410C_STATE_MAX) return false;

    uint16_t moving_dist = (uint16_t)in[3] | ((uint16_t)in[4] << 8);
    uint8_t  moving_eng  = in[5];
    uint16_t static_dist = (uint16_t)in[6] | ((uint16_t)in[7] << 8);
    uint8_t  static_eng  = in[8];

    if (moving_eng > LD2410C_ENERGY_MAX) return false;
    if (static_eng > LD2410C_ENERGY_MAX) return false;
    if (moving_dist > LD2410C_DISTANCE_MAX) return false;
    if (static_dist > LD2410C_DISTANCE_MAX) return false;

    out->target_present     = (state != 0);
    out->moving_distance_cm = moving_dist;
    out->static_distance_cm = static_dist;
    out->moving_energy      = moving_eng;
    out->static_energy      = static_eng;
    out->valid              = true;
    out->failure            = LD2410C_OK;
    return true;
}

// ── Command-mode helpers (V1.09, no SUM/XOR) ──────────────────────────────

int ld2410c_cmd_frame_size(const uint8_t *buf, int avail)
{
    if (avail < LD2410C_CMD_HEAD_LEN + LD2410C_CMD_LEN_FIELD) return 0;
    if (!ld2410c_is_cmd_head(buf)) return -1;

    uint16_t data_len = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    if (data_len < LD2410C_CMD_WORD_LEN) return -1;

    int total = LD2410C_CMD_OVERHEAD + (int)data_len;
    if (total > LD2410C_CMD_MAX_FRAME) return -1;
    if (avail < total) return 0;
    return total;
}

bool ld2410c_cmd_frame_valid(const uint8_t *buf, int total)
{
    if (total < (int)(LD2410C_CMD_HEAD_LEN + LD2410C_CMD_LEN_FIELD
                      + LD2410C_CMD_WORD_LEN + LD2410C_CMD_TAIL_LEN)) {
        return false;
    }
    if (!ld2410c_is_cmd_head(buf)) return false;

    uint16_t data_len = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    int expected = LD2410C_CMD_OVERHEAD + (int)data_len;
    if (total != expected) return false;

    int tail_off = total - LD2410C_CMD_TAIL_LEN;
    if (!ld2410c_is_cmd_tail(buf + tail_off)) return false;

    return true;
}

bool ld2410c_cmd_ack_matches(const uint8_t *buf, int total, uint16_t sent_cmd)
{
    if (!ld2410c_cmd_frame_valid(buf, total)) return false;

    uint16_t ack_cmd = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
    uint16_t expected = sent_cmd | LD2410C_CMD_ACK_MASK;
    return (ack_cmd == expected);
}

uint8_t *ld2410c_build_cmd_frame(uint8_t *frame, int frame_cap, int *out_len,
                                  uint16_t cmd_word,
                                  const uint8_t *data, int data_len)
{
    int data_field_len = LD2410C_CMD_WORD_LEN + data_len;
    int total = LD2410C_CMD_OVERHEAD + data_field_len;
    if (total > frame_cap) {
        *out_len = 0;
        return NULL;
    }

    // Head
    frame[0] = LD2410C_CMD_HEAD1;
    frame[1] = LD2410C_CMD_HEAD2;
    frame[2] = LD2410C_CMD_HEAD3;
    frame[3] = LD2410C_CMD_HEAD4;

    // Length (LE) = bytes from cmd word through end of data
    frame[4] = (uint8_t)(data_field_len & 0xFF);
    frame[5] = (uint8_t)(data_field_len >> 8);

    // Command word (LE)
    frame[6] = (uint8_t)(cmd_word & 0xFF);
    frame[7] = (uint8_t)(cmd_word >> 8);

    // Command data
    for (int i = 0; i < data_len; i++) {
        frame[8 + i] = data[i];
    }

    // Tail
    int tail_off = total - LD2410C_CMD_TAIL_LEN;
    frame[tail_off]     = LD2410C_CMD_TAIL1;
    frame[tail_off + 1] = LD2410C_CMD_TAIL2;
    frame[tail_off + 2] = LD2410C_CMD_TAIL3;
    frame[tail_off + 3] = LD2410C_CMD_TAIL4;

    *out_len = total;
    return frame;
}

// ── High-level command builders (V1.09 §2.2.3 / §2.2.7) ──────────────────

uint8_t *ld2410c_build_write_basic_params(uint8_t *frame, int frame_cap,
                                          int *out_len,
                                          const ld2410c_basic_params_t *p)
{
    if (p == NULL || !ld2410c_basic_params_valid(p)) return NULL;

    // 3 param groups × (2-byte word + 4-byte LE32 value) = 18 bytes data
    uint8_t data[18];
    int o = 0;
    // group 0: max moving distance gate
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_MAX_MOVING_GATE & 0xFF);
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_MAX_MOVING_GATE >> 8);
    data[o++] = p->max_moving_gate; data[o++] = 0; data[o++] = 0; data[o++] = 0;
    // group 1: max static distance gate
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_MAX_STATIC_GATE & 0xFF);
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_MAX_STATIC_GATE >> 8);
    data[o++] = p->max_static_gate; data[o++] = 0; data[o++] = 0; data[o++] = 0;
    // group 2: unoccupied duration
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_UNOCCUPIED_DELAY & 0xFF);
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_UNOCCUPIED_DELAY >> 8);
    data[o++] = (uint8_t)(p->unoccupied_delay_s & 0xFF);
    data[o++] = (uint8_t)(p->unoccupied_delay_s >> 8);
    data[o++] = 0; data[o++] = 0;

    return ld2410c_build_cmd_frame(frame, frame_cap, out_len,
                                   LD2410C_CMD_WRITE_PARAMS, data,
                                   (int)sizeof(data));
}

uint8_t *ld2410c_build_set_sensitivity(uint8_t *frame, int frame_cap,
                                       int *out_len,
                                       uint16_t gate, uint8_t moving,
                                       uint8_t stationary)
{
    if (!ld2410c_sensitivity_valid(gate, moving, stationary)) return NULL;

    uint8_t data[18];
    int o = 0;
    // group 0: distance gate (0..8 or 0xFFFF)
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_GATE & 0xFF);
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_GATE >> 8);
    data[o++] = (uint8_t)(gate & 0xFF);
    data[o++] = (uint8_t)(gate >> 8);
    data[o++] = 0; data[o++] = 0;
    // group 1: moving sensitivity
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_MOVING_SENS & 0xFF);
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_MOVING_SENS >> 8);
    data[o++] = moving; data[o++] = 0; data[o++] = 0; data[o++] = 0;
    // group 2: static sensitivity
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_STATIC_SENS & 0xFF);
    data[o++] = (uint8_t)(LD2410C_PARAM_WORD_STATIC_SENS >> 8);
    data[o++] = stationary; data[o++] = 0; data[o++] = 0; data[o++] = 0;

    return ld2410c_build_cmd_frame(frame, frame_cap, out_len,
                                   LD2410C_CMD_SET_SENS, data,
                                   (int)sizeof(data));
}

int ld2410c_find_cmd_head(const uint8_t *buf, int len)
{
    if (buf == NULL || len < LD2410C_CMD_HEAD_LEN) return -1;
    for (int i = 0; i <= len - LD2410C_CMD_HEAD_LEN; i++) {
        if (ld2410c_is_cmd_head(buf + i)) return i;
    }
    return -1;
}

bool ld2410c_parse_read_params_payload(const uint8_t *data, int data_len,
                                       ld2410c_read_params_t *out)
{
    if (data == NULL || out == NULL) return false;
    if (data_len < 4) return false;          // need AA + N + 2 gates
    if (data[0] != 0xAA) return false;       // header marker
    uint8_t n = data[1];                      // max gate N
    if (n > (LD2410C_N_GATES - 1)) return false;
    // required: AA + N + 2 gates + 2*(n+1) sens + 2 unocc
    int need = 4 + 2 * (n + 1) + 2;
    if (data_len < need) return false;

    out->max_gate        = n;
    out->max_moving_gate = data[2];
    out->max_static_gate = data[3];

    int mo = 4;                  // moving sens[0..N]
    int so = mo + (n + 1);       // static sens[0..N]
    for (uint8_t g = 0; g <= n; g++) {
        out->moving_sens[g] = data[mo + g];
        out->static_sens[g] = data[so + g];
    }
    int uo = so + (n + 1);       // unoccupied duration (LE16)
    out->unoccupied_delay_s = (uint16_t)data[uo] | ((uint16_t)data[uo + 1] << 8);
    return true;
}

bool ld2410c_parse_read_params_ack(const uint8_t *buf, int total,
                                   ld2410c_read_params_t *out)
{
    if (buf == NULL || out == NULL) return false;
    if (!ld2410c_cmd_frame_valid(buf, total)) return false;
    if (!ld2410c_cmd_ack_matches(buf, total, LD2410C_CMD_READ_PARAMS)) return false;
    uint16_t status = (uint16_t)buf[8] | ((uint16_t)buf[9] << 8);
    if (status != 0) return false;
    int payload_len = total - 10 - LD2410C_CMD_TAIL_LEN;
    if (payload_len < 0) return false;
    return ld2410c_parse_read_params_payload(buf + 10, payload_len, out);
}

// ── Range validators ────────────────────────────────────────────────────

bool ld2410c_basic_params_valid(const ld2410c_basic_params_t *p)
{
    if (p == NULL) return false;
    if (p->max_moving_gate < LD2410C_GATE_MIN ||
        p->max_moving_gate > LD2410C_GATE_MAX) return false;
    if (p->max_static_gate < LD2410C_GATE_MIN ||
        p->max_static_gate > LD2410C_GATE_MAX) return false;
    // unoccupied_delay_s is uint16_t — range inherently bounded, no check needed.
    return true;
}

bool ld2410c_sensitivity_valid(uint16_t gate, uint8_t moving, uint8_t stationary)
{
    if (gate != LD2410C_GATE_ALL && gate > (LD2410C_N_GATES - 1)) return false;
    if (moving > LD2410C_SENS_MAX) return false;
    if (stationary > LD2410C_SENS_MAX) return false;
    return true;
}
