// PrivacySense Matter Room Hub - network.h
//
// Wi-Fi Station observer (Phase 3 Step 2). ESP-Matter's ESPWiFiDriver is the
// SOLE owner of Wi-Fi connection management (esp_wifi_set_config / connect /
// disconnect) and credential storage. This module initialises the Wi-Fi stack
// once at boot, subscribes to WIFI/IP events for state observation, publishes
// NETWORK_STATUS to g_app_event_queue, and manages SNTP. It does NOT call
// esp_wifi_set_config / connect / disconnect.
//
// Task profile (task-architecture.md §4.6):
//   - Priority 4 (medium)
//   - Stack   8192 B
//   - Trigger event-driven (ESP-IDF Wi-Fi event loop)
//
// Observer-mode contract: see commissioning-lifecycle.md.

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Lifecycle ---
esp_err_t network_init(void);

// network_task entry point. Created by app_main with stack 8192, prio 4.
void network_task(void *pvParameters);

// Observer-mode stub (Phase 3 Step 2). ESP-Matter's ESPWiFiDriver owns Wi-Fi
// credential injection and NVS storage. This function is retained for ABI
// compatibility with older callers (e.g. network_diag wifi_cred command) but
// performs no work and always returns ESP_ERR_NOT_SUPPORTED. Use a Matter
// controller to commission the device and deliver credentials.
esp_err_t network_apply_provisioned_credentials(const char *ssid, const char *password);

// Query current connection state. Reads from ESP-IDF event state.
bool network_is_connected(void);

// Returns true after first successful SNTP sync. Once true, stays true for
// the remaining power cycle (survives Wi-Fi disconnects). Used by
// state_machine.c::get_current_time() to decide whether wall-clock time is
// reliable enough for NIGHT window auto-switch.
bool network_time_is_synced(void);

// Observer-mode stub. Always returns false — ESP-Matter owns credential NVS
// writes, so there is no local retry state to report. Kept for ABI
// compatibility with the network_diag wifi_status command.
bool network_cred_write_permanent_failure(void);

#ifdef CONFIG_NETWORK_DIAG_CONSOLE
// Diagnostic snapshot published by network_task under mutex protection.
// Fields reflect observer-mode state only — credential/retry/deadline fields
// were removed when network.c stopped owning Wi-Fi connection management.
typedef struct {
    int              state;
    uint8_t          reconnect_attempts;
    uint8_t          auth_fail_attempts;
    bool             provisioned;
    bool             timer_armed;
    uint32_t         ingress_overruns;
} network_diag_info_t;

void network_get_diag_info(network_diag_info_t *info);

// Diagnostic-only: injects 40 DISCONNECTED commands into the ring to exercise
// the spill-slot overload path. No effect on the real Wi-Fi link.
void network_inject_queue_storm(void);
#endif // CONFIG_NETWORK_DIAG_CONSOLE

#ifdef __cplusplus
}
#endif
