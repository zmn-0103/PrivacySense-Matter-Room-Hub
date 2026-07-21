#include "network_diag.h"

#ifdef CONFIG_NETWORK_DIAG_CONSOLE

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "network.h"

static const char *TAG = "network_diag";

static const char *state_name(int state)
{
    switch (state) {
    case 0:  return "DISCONNECTED";
    case 1:  return "CONNECTING";
    case 2:  return "CONNECTED";
    case 3:  return "STOPPED";
    case 4:  return "RECONFIGURING";
    default: return "UNKNOWN";
    }
}

// ── wifi_cred ──────────────────────────────────────────────────────────────
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} s_cred_args;

static int cmd_wifi_cred(int argc, char **argv)
{
    int nerr = arg_parse(argc, argv, (void **)&s_cred_args);
    if (nerr != 0) {
        arg_print_errors(stderr, s_cred_args.end, argv[0]);
        return 1;
    }

    const char *ssid     = s_cred_args.ssid->sval[0];
    const char *password = s_cred_args.password->sval[0];

    printf("wifi_cred: injecting credentials (ssid=\"%s\" pwd_len=%zu)\n",
           ssid, strlen(password));

    esp_err_t ret = network_apply_provisioned_credentials(ssid, password);
    if (ret == ESP_OK) {
        printf("OK: credentials accepted, connecting...\n");
    } else {
        printf("FAIL: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

// ── wifi_status ────────────────────────────────────────────────────────────
static int cmd_wifi_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    network_diag_info_t d;
    network_get_diag_info(&d);

    printf("=== Wi-Fi Status ===\n");
    printf("  state:                  %s (%d)\n", state_name(d.state), d.state);
    printf("  connected:              %s\n", network_is_connected() ? "yes" : "no");
    printf("  reconnect_attempts:     %u\n", (unsigned)d.reconnect_attempts);
    printf("  auth_fail_attempts:     %u\n", (unsigned)d.auth_fail_attempts);
    printf("  provisioned:            %s\n", d.provisioned ? "yes" : "no");
    printf("  timer_armed:            %s\n", d.timer_armed ? "yes" : "no");
    printf("  ingress_overruns:       %" PRIu32 "\n", d.ingress_overruns);
    printf("  cred_write_retry:       %s\n", d.cred_write_retry_pending ? "pending" : "none");
    printf("  wifi_start_retry:       %s\n", d.wifi_start_retry_pending ? "pending" : "none");
    printf("  reconnect_deadline:     %s\n", d.reconnect_deadline_valid ? "armed" : "none");
    printf("  cred_permanent_failure: %s\n", network_cred_write_permanent_failure() ? "yes" : "no");
    printf("  time_synced:            %s\n", network_time_is_synced() ? "yes" : "no");
    printf("========================\n");
    return 0;
}

// ── wifi_fault ─────────────────────────────────────────────────────────────
static struct {
    struct arg_str *type;
    struct arg_end *end;
} s_fault_args;

static int cmd_wifi_fault(int argc, char **argv)
{
    int nerr = arg_parse(argc, argv, (void **)&s_fault_args);
    if (nerr != 0) {
        arg_print_errors(stderr, s_fault_args.end, argv[0]);
        return 1;
    }
    const char *type = s_fault_args.type->sval[0];

    if (strcmp(type, "reconfig_block") == 0) {
        network_inject_fault(NET_FAULT_BLOCK_DISCONNECT_IN_RECONFIG);
        printf("OK: RECONFIGURING disconnect block injected\n");
        printf("  Next wifi_cred while CONNECTED will force RECONFIG_TIMEOUT path\n");
    } else if (strcmp(type, "nvs_fail") == 0) {
        network_inject_fault(NET_FAULT_NVS_WRITE_FAIL);
        printf("OK: NVS write failure injected\n");
        printf("  Next wifi_cred will trigger NVS write retry path\n");
    } else if (strcmp(type, "clear") == 0) {
        network_clear_all_faults();
        printf("OK: all faults cleared\n");
    } else if (strcmp(type, "queue_storm") == 0) {
        printf("Injecting queue storm (40x DISCONNECTED directly into ring)...\n");
        network_inject_queue_storm();
        printf("Storm complete. Check ingress_overruns via wifi_status\n");
    } else {
        printf("UNKNOWN: valid types: reconfig_block, nvs_fail, queue_storm, clear\n");
        return 1;
    }
    return 0;
}

// ── Registration ───────────────────────────────────────────────────────────
void network_diag_register(void)
{
    s_cred_args.ssid    = arg_str1(NULL, NULL, "<ssid>", "Wi-Fi SSID");
    s_cred_args.password = arg_str1(NULL, NULL, "<password>", "Wi-Fi password");
    s_cred_args.end     = arg_end(2);

    const esp_console_cmd_t cred_cmd = {
        .command = "wifi_cred",
        .help = "Inject Wi-Fi credentials and connect: <ssid> <password>",
        .hint = NULL,
        .func = &cmd_wifi_cred,
        .argtable = &s_cred_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cred_cmd));

    const esp_console_cmd_t status_cmd = {
        .command = "wifi_status",
        .help = "Print current Wi-Fi connection state and diagnostics",
        .hint = NULL,
        .func = &cmd_wifi_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));

    s_fault_args.type = arg_str1(NULL, NULL, "<type>", "reconfig_block, nvs_fail, queue_storm, clear");
    s_fault_args.end  = arg_end(1);

    const esp_console_cmd_t fault_cmd = {
        .command = "wifi_fault",
        .help = "Inject fault for acceptance testing: reconfig_block | nvs_fail | queue_storm | clear",
        .hint = NULL,
        .func = &cmd_wifi_fault,
        .argtable = &s_fault_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&fault_cmd));

    ESP_LOGI(TAG, "registered: wifi_cred, wifi_status, wifi_fault");
}

#endif // CONFIG_NETWORK_DIAG_CONSOLE
