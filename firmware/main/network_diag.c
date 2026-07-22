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

    printf("=== Wi-Fi Status (observer mode) ===\n");
    printf("  state:                  %s (%d)\n", state_name(d.state), d.state);
    printf("  connected:              %s\n", network_is_connected() ? "yes" : "no");
    printf("  reconnect_attempts:     %u\n", (unsigned)d.reconnect_attempts);
    printf("  auth_fail_attempts:     %u\n", (unsigned)d.auth_fail_attempts);
    printf("  provisioned:            %s\n", d.provisioned ? "yes" : "no");
    printf("  timer_armed:            %s\n", d.timer_armed ? "yes" : "no");
    printf("  ingress_overruns:       %" PRIu32 "\n", d.ingress_overruns);
    printf("  cred_owner:             ESP-Matter (observer mode)\n");
    printf("  time_synced:            %s\n", network_time_is_synced() ? "yes" : "no");
    printf("=====================================\n");
    return 0;
}

// ── wifi_queue_storm ───────────────────────────────────────────────────────
// Diagnostic-only: injects 40 DISCONNECTED commands directly into the ring to
// exercise the spill-slot overload path. The reconfig_block / nvs_fail / clear
// fault-injection sub-commands were removed in Phase 3 Step 2 when network.c
// became a Wi-Fi observer (no local credential/retry state to fault).
static int cmd_wifi_queue_storm(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Injecting queue storm (40x DISCONNECTED directly into ring)...\n");
    network_inject_queue_storm();
    printf("Storm complete. Check ingress_overruns via wifi_status\n");
    return 0;
}

// ── Registration ───────────────────────────────────────────────────────────
void network_diag_register(void)
{
    s_cred_args.ssid    = arg_str1(NULL, NULL, "<ssid>", "Wi-Fi SSID");
    s_cred_args.password = arg_str1(NULL, NULL, "<password>", "Wi-Fi password");
    s_cred_args.end     = arg_end(2);

    // wifi_cred is a no-op in observer mode (ESP-Matter owns credentials).
    // Retained so existing test scripts do not fail at registration; it will
    // print FAIL: ESP_ERR_NOT_SUPPORTED when invoked.
    const esp_console_cmd_t cred_cmd = {
        .command = "wifi_cred",
        .help = "[observer-mode no-op] Credentials are owned by ESP-Matter; "
                "use a Matter controller to commission. Kept for ABI compat.",
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

    const esp_console_cmd_t storm_cmd = {
        .command = "wifi_queue_storm",
        .help = "Inject 40 DISCONNECTED cmds to exercise ring spill-slot overload path",
        .hint = NULL,
        .func = &cmd_wifi_queue_storm,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&storm_cmd));

    ESP_LOGI(TAG, "registered: wifi_cred (no-op), wifi_status, wifi_queue_storm");
}

#endif // CONFIG_NETWORK_DIAG_CONSOLE
