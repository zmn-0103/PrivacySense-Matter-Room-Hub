// PrivacySense Matter Room Hub - network.h
//
// Wi-Fi Station management. Credentials are NOT compiled in — they are
// injected at runtime via BLE commissioning (esp_matter Wi-Fi provisioning)
// and stored in NVS by ESP-IDF Wi-Fi stack.
//
// Task profile (task-architecture.md §4.6):
//   - Priority 4 (medium)
//   - Stack   8192 B
//   - Trigger event-driven (ESP-IDF Wi-Fi event loop)
//
// Disconnect / reconnect / backoff policy: see commissioning-lifecycle.md.

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

// Called by esp_matter Wi-Fi provisioning callback when credentials arrive.
// Stores credentials via ESP-IDF Wi-Fi API (never logs the passphrase).
esp_err_t network_apply_provisioned_credentials(const char *ssid, const char *password);

// Query current connection state. Reads from ESP-IDF event state.
bool network_is_connected(void);

// Returns true after first successful SNTP sync. Once true, stays true for
// the remaining power cycle (survives Wi-Fi disconnects). Used by
// state_machine.c::get_current_time() to decide whether wall-clock time is
// reliable enough for NIGHT window auto-switch.
bool network_time_is_synced(void);

// Returns true if credential NVS write has permanently failed (max retries
// exhausted). The Matter layer should check this and report provisioning
// failure instead of treating it as success.
bool network_cred_write_permanent_failure(void);

#ifdef CONFIG_NETWORK_DIAG_CONSOLE
// Diagnostic snapshot published by network_task under mutex protection.
typedef struct {
    int              state;
    uint8_t          reconnect_attempts;
    uint8_t          auth_fail_attempts;
    bool             provisioned;
    bool             timer_armed;
    uint32_t         ingress_overruns;
    bool             cred_write_retry_pending;
    bool             wifi_start_retry_pending;
    bool             reconnect_deadline_valid;
} network_diag_info_t;

void network_get_diag_info(network_diag_info_t *info);

// Fault injection.
typedef enum {
    NET_FAULT_NONE = 0,
    NET_FAULT_BLOCK_DISCONNECT_IN_RECONFIG,
    NET_FAULT_NVS_WRITE_FAIL,
} net_fault_type_t;

void network_inject_fault(net_fault_type_t fault);
void network_clear_fault(net_fault_type_t fault);
void network_clear_all_faults(void);
void network_inject_queue_storm(void);
#endif // CONFIG_NETWORK_DIAG_CONSOLE

#ifdef __cplusplus
}
#endif
