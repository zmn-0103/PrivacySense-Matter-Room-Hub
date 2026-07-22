// PrivacySense Matter Room Hub - matter_app.cpp
//
// Minimal Matter node + BLE commissioning (Phase 3 Step 2).
//
// This file replaces the previous matter_app.c STUB. It creates a minimal
// Matter node with only Endpoint 0 (Root Node) and starts BLE commissioning
// using the Test Commissionable Data Provider. Business endpoints (EP1
// OccupancySensing, EP2 ModeSelect) and attribute sync arrive in Phase 4.
//
// Endpoint topology (current phase):
//   EP0 Root Node  (standard, created automatically by node::create())
//   EP1/EP2        NOT created yet (Phase 4)
//
// SECURITY: setup passcode / discriminator / SPAKE2+ verifier come from a
// Commissionable Data Provider per commissioning-lifecycle.md §3.2 — they are
// NOT "randomly generated on first boot". Current build uses the Test Provider
// (fixed passcode/discriminator, dev only) enabled in sdkconfig.defaults:
//   CONFIG_EXAMPLE_COMMISSIONABLE_DATA_PROVIDER=y
// The QR / manual-code setup payload is derived from these values and presented
// OUT-OF-BAND for the user to scan / type into the controller. BLE then carries
// the PASE commissioning session that uses the passcode. We MUST NOT:
//   - Hardcode passcode/discriminator in source or sdkconfig.defaults.
//   - Log the QR code at INFO level (use ESP_LOG_DEBUG, gated by menuconfig).
//   - Persist any of these in tests/evidence/ Markdown files.
//
// Wi-Fi ownership (resolved Phase 3 Step 2 per Reviewer AI directive):
//   ESP-Matter's ESPWiFiDriver (CHIP platform layer) is the SOLE owner of
//   Wi-Fi connection management. When a controller commissions the device and
//   sends Wi-Fi credentials via the Network Commissioning cluster,
//   ESPWiFiDriver directly calls esp_wifi_set_config() + esp_wifi_connect().
//   It also handles internal reconnect on link drop.
//
//   network.c is now a Wi-Fi OBSERVER only:
//     - Initialises the Wi-Fi stack (esp_wifi_init / set_mode / start) once
//       at boot so ESPWiFiDriver inherits an already-started STA.
//     - Subscribes to WIFI_EVENT / IP_EVENT for state observation.
//     - Publishes NETWORK_STATUS to app_event_queue for state_machine_task.
//     - Manages SNTP time sync.
//     - Does NOT call esp_wifi_set_config / connect / disconnect.
//     - network_apply_provisioned_credentials() is a no-op stub returning
//       ESP_ERR_NOT_SUPPORTED (ESP-Matter owns credential injection).
//   The local reconnect SM (network_reconnect_sm.c) still tracks link state
//   via events, but its WIFI_CONNECT / START_TIMER actions are ignored by
//   network.c's execute_action. ESP-Matter + ESP-IDF auto-connect handle all
//   connect / reconnect / reconfig.
//
// Task contract (task-architecture.md §4.7):
//   - matter_adapter_task is NOT a separate Matter event loop. ESP-Matter
//     already runs its own internal CHIP task. This task is a thin adapter
//     that consumes g_matter_report_queue and applies attribute updates on
//     the CHIP stack context.
//   - 2 s queue wait timeout → feed TWDT and re-check health.
//   - ChangeToMode responses are routed through this task to keep the actual
//     esp_matter call on the CHIP stack context.

#include "matter_app.h"

#include <esp_matter.h>
#include <esp_matter_endpoint.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "state_machine.h"   // g_matter_report_queue, matter_report_t

using namespace esp_matter;
using namespace chip::DeviceLayer;

static const char *TAG = "matter_app";

// Endpoint handles (matter-data-model.md §2, v0.2).
// EP0 is created automatically by node::create(). EP1/EP2 are Phase 4.
static node_t *s_node = nullptr;

#define MATTER_ADAPTER_QUEUE_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)

// ─── ESP-Matter callbacks ───

// Event callback: called by the ESP-Matter CHIP task for platform events.
// Phase 3 Step 2 only logs commissioning / IP events; room_state integration
// is Phase 3 Step 3.
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case DeviceEventType::kInterfaceIpAddressChanged:
        if (event->InterfaceIpAddressChanged.Type == InterfaceIpChangeType::kIpV4_Assigned) {
            ESP_LOGI(TAG, "event: IPv4 assigned");
        } else if (event->InterfaceIpAddressChanged.Type == InterfaceIpChangeType::kIpV4_Lost) {
            ESP_LOGI(TAG, "event: IPv4 lost");
        }
        break;

    case DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "event: commissioning session started");
        break;

    case DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "event: commissioning session stopped");
        break;

    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "event: commissioning window opened (BLE advertising)");
        break;

    case DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "event: commissioning window closed");
        break;

    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "event: commissioning complete (fabric established)");
        // Phase 3 Step 3: update room_state.matter_commissioned = true
        // via app_event_queue → state_machine_task.
        break;

    case DeviceEventType::kFabricRemoved:
        // A fabric was removed, but this is NOT necessarily a factory reset.
        // In multi-admin scenarios, only one of several fabrics may be gone.
        // Phase 3 Step 5 should check remaining fabric count before deciding
        // whether to re-open commissioning or clear business config.
        ESP_LOGI(TAG, "event: fabric removed (check remaining fabric count)");
        break;

    default:
        break;
    }
}

// Attribute update callback: called when a controller writes an attribute.
// Phase 3 Step 2 has no business endpoints, so this is a no-op. Phase 4 will
// handle EP1/EP2 attribute writes (ChangeToMode on EP2 → app_event_queue).
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id,
                                         uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    (void)type;
    (void)endpoint_id;
    (void)cluster_id;
    (void)attribute_id;
    (void)val;
    (void)priv_data;
    // Return ESP_OK for all attribute updates — EP0 root node attributes are
    // handled internally by ESP-Matter. EP1/EP2 arrive in Phase 4.
    return ESP_OK;
}

// Identification callback: called when the controller triggers identify.
// Phase 3 Step 2 has no business endpoints, so this is a no-op.
static esp_err_t app_identification_cb(identification::callback_type_t type,
                                       uint16_t endpoint_id,
                                       uint8_t effect_id,
                                       uint8_t effect_variant,
                                       void *priv_data)
{
    (void)type;
    (void)endpoint_id;
    (void)effect_id;
    (void)effect_variant;
    (void)priv_data;
    return ESP_OK;
}

// ─── Public API (C ABI, declared in matter_app.h) ───

esp_err_t matter_app_init(void)
{
    if (s_node != nullptr) {
        ESP_LOGW(TAG, "matter_app_init called twice");
        return ESP_ERR_INVALID_STATE;
    }

    // Create minimal node with only EP0 (Root Node). node::create() internally
    // adds the root node device type to endpoint 0. No business endpoints
    // (EP1/EP2) are created in this phase — Phase 4 will add them via
    // endpoint::occupancy_sensor::create() and endpoint::mode_select::create().
    node::config_t node_config;
    s_node = node::create(&node_config,
                          app_attribute_update_cb,
                          app_identification_cb);
    if (s_node == nullptr) {
        ESP_LOGE(TAG, "node::create failed");
        return ESP_FAIL;
    }

    // Start the Matter stack. This spawns the internal CHIP task, initialises
    // BLE (NimBLE), opens the commissioning window, and begins advertising.
    //
    // Wi-Fi stack init is handled internally by ESP-Matter via
    // ESP32Utils::InitWiFiStack(). ESP-Matter's ESPWiFiDriver is the SOLE
    // Wi-Fi connection owner — see the "Wi-Fi ownership" note in the file
    // header. network.c is an observer and does NOT call set_config/connect.
    //
    // SECURITY: the setup payload (QR code / manual code) is derived from the
    // Commissionable Data Provider (Test Provider for dev). It is NOT logged
    // at INFO — the controller gets it out-of-band (e.g. printed on a label).
    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter::start failed: %s", esp_err_to_name(err));
        // Note: esp_matter::start may have partially initialised CHIP/NVS
        // before returning an error. A full rollback is not exposed by the
        // ESP-Matter API; we null s_node so matter_adapter_task is not
        // spawned (main.c gates the spawn on matter_app_init's return) and
        // the device continues in Matter-degraded mode until reboot.
        s_node = nullptr;
        return err;
    }

    // Do NOT log "commissioning window open" here — esp_matter::start() does
    // not guarantee the window is open at the moment this function returns.
    // The kCommissioningWindowOpened event callback above is the authoritative
    // signal that advertising has begun. Init success only means the stack
    // started; the window may open later (or be already open on a fresh boot).
    ESP_LOGI(TAG, "Matter stack started (EP0 only, BLE commissioning ready)");
    return ESP_OK;
}

esp_err_t matter_app_respond_change_to_mode(void *cmd_ctx, bool success)
{
    if (cmd_ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Phase 4: EP2 ModeSelect ChangeToMode response. No EP2 exists yet, so
    // this remains a stub. Callers (state_machine_task) log and continue.
    ESP_LOGW(TAG, "STUB: ChangeToMode response NOT sent (ctx=%p, success=%d) — "
             "EP2 ModeSelect not wired yet (Phase 4)",
             cmd_ctx, success ? 1 : 0);
    (void)success;
    return ESP_ERR_NOT_SUPPORTED;
}

void matter_adapter_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    for (;;) {
        matter_report_t report;
        // Block up to 2 s waiting for a report; the timeout also serves as
        // the TWDT feed cadence (task-architecture.md §7.2).
        if (xQueueReceive(g_matter_report_queue, &report,
                          pdMS_TO_TICKS(MATTER_ADAPTER_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            // Phase 4: apply the report to Matter attributes under the CHIP
            // stack lock. Mapping (matter-data-model.md §3, §4, §6):
            //   - MATTER_REPORT_OCCUPANCY:
            //       EP1 occupancy = (report.occupancy == OCCUPANCY_OCCUPIED) ? 1 : 0
            //       Skip when report.occupancy == OCCUPANCY_UNKNOWN — keep last
            //       valid value in Matter (state-model.md §5.1).
            //   - MATTER_REPORT_CURRENT_MODE:
            //       EP2 CurrentMode = (uint8_t)report.user_mode
            //   - MATTER_REPORT_FORCE_SYNC (Wi-Fi reconnected):
            //       Re-report EP1 occupancy, then EP2 CurrentMode
            //       (matter-data-model.md §6.3).
            //
            // No EP1/EP2 exist yet, so reports are consumed and dropped.
            ESP_LOGD(TAG, "report type=%d (Phase 4 will apply)", (int)report.type);
        }
        // Timeout with no report → fall through to TWDT feed. This is the
        // 2 s health-check heartbeat required by task-architecture.md §7.2.

        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}
