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

#ifdef __cplusplus
}
#endif
