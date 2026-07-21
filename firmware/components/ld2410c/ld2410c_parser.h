// PrivacySense Matter Room Hub - LD2410C V1.09 frame parser (internal header)
//
// Shared between ld2410c.c (production) and test_ld2410c_parser.c (test).
// NOT part of the public API; not in include/.
//
// This header is intentionally self-contained (no ESP-IDF includes) so the
// host-side unit test can compile it without the ESP-IDF toolchain. The
// struct definitions here MUST match include/ld2410c.h.
//
// Protocol reference: "LD2410C 串口通信协议 V1.09.pdf" (2025-06-09).
//
// ── Normal-mode data frame (continuous reporting) ──────────────────────
//   [0-3]   Head:           0xF4 0xF3 0xF2 0xF1
//   [4-5]   Length:         intra-frame data length (LE)
//   [6]     Data type:      0x02 = normal mode
//   [7]     Marker:         0xAA
//   [8]     Target state:   0=none, 1=moving, 2=static, 3=both
//   [9-10]  Moving dist:    cm (LE)
//   [11]    Moving energy
//   [12-13] Static dist:    cm (LE)
//   [14]    Static energy
//   [15-16] Detection dist: cm (LE)
//   [17-18] Tail verify:    0x55 0x00
//   [19-22] End of frame:   0xF8 0xF7 0xF6 0xF5
// Total: 23 bytes (for normal mode, length=0x0D)
//
// ── Command/ACK frame (configuration) ──────────────────────────────────
//   [0-3]   Head:           0xFD 0xFC 0xFB 0xFA
//   [4-5]   Length:         LE uint16 — bytes from cmd through end-of-data
//   [6-7]   Cmd:            LE uint16 — command word (host→radar)
//                           Original | 0x0100 (radar→host ACK)
//   [8-9]   Status:         LE uint16 — 0=success (ACK only, absent in cmd)
//   [10..]  Data:           command/response payload
//   [n+1..n+4] Tail:        0x04 0x03 0x02 0x01
// No SUM/XOR checksum in V1.09. Validation is by head, length, and tail.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // NULL

#ifdef __cplusplus
extern "C" {
#endif

// ── Normal-mode data frame constants ───────────────────────────────────────
#define LD2410C_FRAME_HEAD1     0xF4
#define LD2410C_FRAME_HEAD2     0xF3
#define LD2410C_FRAME_HEAD3     0xF2
#define LD2410C_FRAME_HEAD4     0xF1
#define LD2410C_FRAME_TAIL1     0xF8
#define LD2410C_FRAME_TAIL2     0xF7
#define LD2410C_FRAME_TAIL3     0xF6
#define LD2410C_FRAME_TAIL4     0xF5
#define LD2410C_TAIL_VERIFY1    0x55
#define LD2410C_TAIL_VERIFY2    0x00
#define LD2410C_DATA_TYPE_NORMAL  0x02
#define LD2410C_DATA_TYPE_ENGINEER 0x01
#define LD2410C_AA_MARKER       0xAA

#define LD2410C_FRAME_HEAD_LEN     4
#define LD2410C_FRAME_LEN_FIELD    2
#define LD2410C_FRAME_TAIL_LEN     4
#define LD2410C_FRAME_TAIL_VERIFY  2
#define LD2410C_FRAME_MIN_LEN     13
#define LD2410C_FRAME_ENG_LEN     35

#define LD2410C_STATE_MAX          3
#define LD2410C_ENERGY_MAX       100
#define LD2410C_DISTANCE_MAX    600

#define LD2410C_RX_BUF_CAP   256

// ── Shared types (MUST match include/ld2410c.h) ────────────────────────────
typedef enum {
    LD2410C_OK           = 0,
    LD2410C_FAIL_TIMEOUT,
    LD2410C_FAIL_PROTOCOL,
} ld2410c_failure_t;

typedef struct {
    uint32_t   timestamp_ms;
    bool       target_present;
    uint16_t   moving_distance_cm;
    uint16_t   static_distance_cm;
    uint8_t    moving_energy;
    uint8_t    static_energy;
    bool       valid;
    ld2410c_failure_t failure;
} ld2410c_radar_data_t;

// ── Command/ACK frame constants (V1.09, no SUM/XOR) ────────────────────────
#define LD2410C_CMD_HEAD1      0xFD
#define LD2410C_CMD_HEAD2      0xFC
#define LD2410C_CMD_HEAD3      0xFB
#define LD2410C_CMD_HEAD4      0xFA
#define LD2410C_CMD_TAIL1      0x04
#define LD2410C_CMD_TAIL2      0x03
#define LD2410C_CMD_TAIL3      0x02
#define LD2410C_CMD_TAIL4      0x01

#define LD2410C_CMD_HEAD_LEN   4
#define LD2410C_CMD_LEN_FIELD  2
#define LD2410C_CMD_TAIL_LEN   4
#define LD2410C_CMD_WORD_LEN   2       // 2-byte LE command word
#define LD2410C_CMD_STATUS_LEN 2       // 2-byte LE status in ACK

#define LD2410C_CMD_MAX_DATA   32

// Frame overhead = head + len_field + tail = 10 bytes
#define LD2410C_CMD_OVERHEAD   (LD2410C_CMD_HEAD_LEN + LD2410C_CMD_LEN_FIELD \
                                + LD2410C_CMD_TAIL_LEN)
#define LD2410C_CMD_MAX_FRAME  (LD2410C_CMD_OVERHEAD + LD2410C_CMD_WORD_LEN \
                                + LD2410C_CMD_MAX_DATA)

// V1.09 2-byte LE command words (per official spec)
#define LD2410C_CMD_ENABLE_CONF   0x00FF
#define LD2410C_CMD_DISABLE_CONF  0x00FE
#define LD2410C_CMD_WRITE_PARAMS  0x0060
#define LD2410C_CMD_READ_PARAMS   0x0061
#define LD2410C_CMD_ENG_MODE_ON   0x0062
#define LD2410C_CMD_ENG_MODE_OFF  0x0063
#define LD2410C_CMD_SET_SENS      0x0064
#define LD2410C_CMD_FW_VERSION    0x00A0
#define LD2410C_CMD_SET_BAUD      0x00A1
#define LD2410C_CMD_FACTORY_RESET 0x00A2
#define LD2410C_CMD_RESTART       0x00A3

// ACK mask: response cmd = original_cmd | 0x0100
#define LD2410C_CMD_ACK_MASK      0x0100

// 0x0060 parameter words (LE uint16 keys for the multi-param set command)
#define LD2410C_PARAM_WORD_MAX_MOVING_GATE  0x0000
#define LD2410C_PARAM_WORD_MAX_STATIC_GATE  0x0001
#define LD2410C_PARAM_WORD_UNOCCUPIED_DELAY 0x0002

// 0x0064 sensitivity parameter words (V1.09 §2.2.7)
#define LD2410C_PARAM_WORD_GATE         0x0000
#define LD2410C_PARAM_WORD_MOVING_SENS  0x0001
#define LD2410C_PARAM_WORD_STATIC_SENS  0x0002

// ACK timeout: max wait for a radar response per command transaction
#define LD2410C_CMD_TIMEOUT_MS  500

// ── High-level parameter types & value ranges (V1.09 §1.2.2, §2.2) ───────
#define LD2410C_GATE_MIN           2       // config range for max distance gates
#define LD2410C_GATE_MAX           8
#define LD2410C_GATE_ALL           0xFFFF  // "all gates" for 0x0064
#define LD2410C_SENS_MIN           0
#define LD2410C_SENS_MAX           100
#define LD2410C_UNOCCUPIED_MAX     65535U
#define LD2410C_N_GATES            9       // gates 0..8

// Basic config written by 0x0060 (max moving/static distance gate + unocc delay)
typedef struct {
    uint8_t  max_moving_gate;    // 2..8
    uint8_t  max_static_gate;    // 2..8
    uint16_t unoccupied_delay_s; // 0..65535
} ld2410c_basic_params_t;

// Full parameter snapshot returned by 0x0061 read-all-params ACK
typedef struct {
    uint8_t  max_gate;                  // N (e.g. 8)
    uint8_t  max_moving_gate;           // configured max moving gate
    uint8_t  max_static_gate;           // configured max static gate
    uint8_t  moving_sens[LD2410C_N_GATES];
    uint8_t  static_sens[LD2410C_N_GATES];
    uint16_t unoccupied_delay_s;
} ld2410c_read_params_t;

// ── High-level command builders (V1.09 §2.2.3 / §2.2.7) ──────────────────

// Build a 0x0060 "write basic params" frame (3 param groups, length 0x0014).
// Returns frame pointer, or NULL if frame_cap is too small or p is invalid.
uint8_t *ld2410c_build_write_basic_params(uint8_t *frame, int frame_cap,
                                          int *out_len,
                                          const ld2410c_basic_params_t *p);

// Build a 0x0064 "set gate sensitivity" frame (3 param groups, length 0x0014).
// gate: 0..8 (single gate) or LD2410C_GATE_ALL (0xFFFF, all gates).
// Returns frame pointer, or NULL if frame_cap too small or args invalid.
uint8_t *ld2410c_build_set_sensitivity(uint8_t *frame, int frame_cap,
                                       int *out_len,
                                       uint16_t gate, uint8_t moving,
                                       uint8_t stationary);

// Parse a 0x0061 read-all-params ACK frame (38 bytes). Returns true on
// success and fills *out. Validates head, ACK cmd match, status, and 0xAA
// header in the data field.
bool ld2410c_parse_read_params_ack(const uint8_t *buf, int total,
                                   ld2410c_read_params_t *out);

// Parse the 0x0061 ACK *payload* (the data field starting at the 0xAA header,
// i.e. what the transaction core returns as rx_buf). Same output as above.
bool ld2410c_parse_read_params_payload(const uint8_t *data, int data_len,
                                       ld2410c_read_params_t *out);

// Scan buf[0..len-1] for the command head FD FC FB FA. Returns the offset of
// the first match, or -1 if not found. Used for ACK re-synchronisation.
int ld2410c_find_cmd_head(const uint8_t *buf, int len);

// Range validators — return true if the value set is legal per V1.09.
bool ld2410c_basic_params_valid(const ld2410c_basic_params_t *p);
bool ld2410c_sensitivity_valid(uint16_t gate, uint8_t moving, uint8_t stationary);

// ── Normal-mode parser ───────────────────────────────────────────────────

// Check for 4-byte data-frame head: F4 F3 F2 F1
static inline bool ld2410c_is_head(const uint8_t *p)
{
    return p[0] == LD2410C_FRAME_HEAD1 &&
           p[1] == LD2410C_FRAME_HEAD2 &&
           p[2] == LD2410C_FRAME_HEAD3 &&
           p[3] == LD2410C_FRAME_HEAD4;
}

// Check for 4-byte data-frame tail: F8 F7 F6 F5
static inline bool ld2410c_is_tail(const uint8_t *p)
{
    return p[0] == LD2410C_FRAME_TAIL1 &&
           p[1] == LD2410C_FRAME_TAIL2 &&
           p[2] == LD2410C_FRAME_TAIL3 &&
           p[3] == LD2410C_FRAME_TAIL4;
}

int ld2410c_frame_total_size(const uint8_t *buf, int avail);
bool ld2410c_try_parse_frame(const uint8_t *buf, int avail,
                             ld2410c_radar_data_t *out);

// ── Command-frame helpers (self-contained, no ESP-IDF) ────────────────────

// Check for 4-byte command head: FD FC FB FA
static inline bool ld2410c_is_cmd_head(const uint8_t *p)
{
    return p[0] == LD2410C_CMD_HEAD1 &&
           p[1] == LD2410C_CMD_HEAD2 &&
           p[2] == LD2410C_CMD_HEAD3 &&
           p[3] == LD2410C_CMD_HEAD4;
}

// Check for 4-byte command tail: 04 03 02 01
static inline bool ld2410c_is_cmd_tail(const uint8_t *p)
{
    return p[0] == LD2410C_CMD_TAIL1 &&
           p[1] == LD2410C_CMD_TAIL2 &&
           p[2] == LD2410C_CMD_TAIL3 &&
           p[3] == LD2410C_CMD_TAIL4;
}

// Determine total size of a command/ACK frame at buf[0].
// Returns >0 on success, 0 = NEED_MORE, -1 = INVALID.
int ld2410c_cmd_frame_size(const uint8_t *buf, int avail);

// Validate a complete command/ACK frame (head, length bounds, tail).
// total must be the value returned by ld2410c_cmd_frame_size().
bool ld2410c_cmd_frame_valid(const uint8_t *buf, int total);

// Check whether a received ACK frame at buf[0] is the response to `sent_cmd`.
// Returns true if cmd_frame_valid && head + cmd_word match |0x0100.
bool ld2410c_cmd_ack_matches(const uint8_t *buf, int total,
                             uint16_t sent_cmd);

// Build a command frame into `frame` (caller provides buffer >=
// LD2410C_CMD_MAX_FRAME). Fills *out_len. Returns frame pointer, or
// NULL if frame_cap is too small.
uint8_t *ld2410c_build_cmd_frame(uint8_t *frame, int frame_cap, int *out_len,
                                  uint16_t cmd_word,
                                  const uint8_t *data, int data_len);

#ifdef __cplusplus
}
#endif
