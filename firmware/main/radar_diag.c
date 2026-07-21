// PrivacySense Matter Room Hub - Radar diagnostic console commands
//
// Interactive UART console for R11 radar configuration testing.
// Registered in main.c; build with the existing ESP-IDF esp_console component.
// Commands:
//   radar_read       — read & print current radar parameters
//   radar_write      — write basic params: <max_moving_gate> <max_static_gate> <unocc_delay_s>
//   radar_sensitivity — set gate sensitivity: <gate> <moving> <stationary>
//   radar_uncertain  — print whether radar state is uncertain

#include "radar_diag.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "ld2410c.h"

static const char *TAG = "radar_diag";

// ── radar_read ───────────────────────────────────────────────────────────
static int cmd_radar_read(int argc, char **argv)
{
    (void)argc; (void)argv;
    ld2410c_read_params_t p;
    esp_err_t r = ld2410c_read_params(&p);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "ld2410c_read_params failed: %s", esp_err_to_name(r));
        printf("FAIL: %s\n", esp_err_to_name(r));
        return 1;
    }
    printf("=== Radar Parameters ===\n");
    printf("  max_gate:         %u\n", (unsigned)p.max_gate);
    printf("  max_moving_gate:  %u\n", (unsigned)p.max_moving_gate);
    printf("  max_static_gate:  %u\n", (unsigned)p.max_static_gate);
    printf("  unoccupied_delay: %u s\n", (unsigned)p.unoccupied_delay_s);
    printf("  uncertain:        %s\n",
           ld2410c_radar_state_uncertain() ? "YES" : "no");
    printf("  sensitivity (gate moving/static):\n");
    for (int i = 0; i <= (int)p.max_gate && i < LD2410C_N_GATES; i++) {
        printf("    gate %2d: moving=%3u  static=%3u\n",
               i,
               (unsigned)p.moving_sens[i],
               (unsigned)p.static_sens[i]);
    }
    printf("========================\n");
    return 0;
}

// ── radar_write ─────────────────────────────────────────────────────────
static struct {
    struct arg_int *max_moving;
    struct arg_int *max_static;
    struct arg_int *unocc_delay;
    struct arg_end *end;
} s_write_args;

static int cmd_radar_write(int argc, char **argv)
{
    int nerr = arg_parse(argc, argv, (void **)&s_write_args);
    if (nerr != 0) {
        arg_print_errors(stderr, s_write_args.end, argv[0]);
        return 1;
    }
    // Validate raw int BEFORE casting — out-of-range signed values could
    // wrap on conversion to uint8_t/uint16_t (Reviewer P1).
    int raw_moving = s_write_args.max_moving->ival[0];
    int raw_static = s_write_args.max_static->ival[0];
    int raw_delay  = s_write_args.unocc_delay->ival[0];
    if (raw_moving < 0 || raw_moving > 8 ||
        raw_static < 0 || raw_static > 8 ||
        raw_delay  < 0 || raw_delay  > 65535) {
        printf("INVALID: param values out of range (max_gate 0-8, delay 0-65535)\n");
        return 1;
    }
    ld2410c_basic_params_t p = {
        .max_moving_gate   = (uint8_t)raw_moving,
        .max_static_gate   = (uint8_t)raw_static,
        .unoccupied_delay_s = (uint16_t)raw_delay,
    };
    esp_err_t r = ld2410c_write_basic_params(&p);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "ld2410c_write_basic_params failed: %s", esp_err_to_name(r));
        printf("FAIL: %s\n", esp_err_to_name(r));
        return 1;
    }
    printf("OK: written (moving=%u static=%u unocc=%us)\n",
           (unsigned)p.max_moving_gate, (unsigned)p.max_static_gate,
           (unsigned)p.unoccupied_delay_s);
    printf("  uncertain: %s\n",
           ld2410c_radar_state_uncertain() ? "YES" : "no");
    return 0;
}

// ── radar_sensitivity ───────────────────────────────────────────────────
static struct {
    struct arg_int *gate;
    struct arg_int *moving;
    struct arg_int *stationary;
    struct arg_end *end;
} s_sens_args;

static int cmd_radar_sensitivity(int argc, char **argv)
{
    int nerr = arg_parse(argc, argv, (void **)&s_sens_args);
    if (nerr != 0) {
        arg_print_errors(stderr, s_sens_args.end, argv[0]);
        return 1;
    }
    // Validate raw int BEFORE casting — out-of-range signed values could
    // wrap on conversion to uint16_t/uint8_t (Reviewer P1).
    int raw_gate = s_sens_args.gate->ival[0];
    int raw_mov  = s_sens_args.moving->ival[0];
    int raw_stat = s_sens_args.stationary->ival[0];
    // gate: 0-8 or LD2410C_GATE_ALL (65535); sens: 0-100
    if ((raw_gate < 0 || raw_gate > 8) && raw_gate != 65535) {
        printf("INVALID: gate must be 0-8 or 65535 (all)\n");
        return 1;
    }
    if (raw_mov < 0 || raw_mov > 100 || raw_stat < 0 || raw_stat > 100) {
        printf("INVALID: sensitivity must be 0-100\n");
        return 1;
    }
    uint16_t gate = (uint16_t)raw_gate;
    uint8_t  mov  = (uint8_t)raw_mov;
    uint8_t  stat = (uint8_t)raw_stat;
    esp_err_t r = ld2410c_set_gate_sensitivity(gate, mov, stat);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "ld2410c_set_gate_sensitivity failed: %s", esp_err_to_name(r));
        printf("FAIL: %s\n", esp_err_to_name(r));
        return 1;
    }
    printf("OK: sensitivity written (gate=%u moving=%u stationary=%u)\n",
           (unsigned)gate, (unsigned)mov, (unsigned)stat);
    printf("  uncertain: %s\n",
           ld2410c_radar_state_uncertain() ? "YES" : "no");
    return 0;
}

// ── radar_uncertain ──────────────────────────────────────────────────────
static int cmd_radar_uncertain(int argc, char **argv)
{
    (void)argc; (void)argv;
    bool u = ld2410c_radar_state_uncertain();
    printf("radar_state_uncertain: %s\n", u ? "YES" : "no");
    return 0;
}

// ── Registration ─────────────────────────────────────────────────────────
void radar_diag_register(void)
{
    s_write_args.max_moving = arg_int1(NULL, NULL, "<0-8>", "max moving distance gate");
    s_write_args.max_static = arg_int1(NULL, NULL, "<0-8>", "max static distance gate");
    s_write_args.unocc_delay = arg_int1(NULL, NULL, "<s>", "unoccupied delay in seconds");
    s_write_args.end = arg_end(3);

    s_sens_args.gate       = arg_int1(NULL, NULL, "<0-8|65535>", "gate (0-8 or 65535 for all)");
    s_sens_args.moving     = arg_int1(NULL, NULL, "<0-100>", "moving sensitivity");
    s_sens_args.stationary = arg_int1(NULL, NULL, "<0-100>", "stationary sensitivity");
    s_sens_args.end = arg_end(3);

    const esp_console_cmd_t read_cmd = {
        .command = "radar_read",
        .help = "Read and display current radar parameters",
        .hint = NULL,
        .func = &cmd_radar_read,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&read_cmd));

    const esp_console_cmd_t write_cmd = {
        .command = "radar_write",
        .help = "Write basic params: <max_moving_gate> <max_static_gate> <unocc_delay_s>",
        .hint = NULL,
        .func = &cmd_radar_write,
        .argtable = &s_write_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&write_cmd));

    const esp_console_cmd_t sens_cmd = {
        .command = "radar_sensitivity",
        .help = "Set gate sensitivity: <gate> <moving> <stationary>",
        .hint = NULL,
        .func = &cmd_radar_sensitivity,
        .argtable = &s_sens_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&sens_cmd));

    const esp_console_cmd_t uncert_cmd = {
        .command = "radar_uncertain",
        .help = "Print whether radar state is uncertain",
        .hint = NULL,
        .func = &cmd_radar_uncertain,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&uncert_cmd));

    ESP_LOGI(TAG, "registered: radar_read, radar_write, radar_sensitivity, radar_uncertain");
}
