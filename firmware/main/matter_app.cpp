// PrivacySense Matter Room Hub - matter_app.cpp
//
// Matter node + BLE commissioning + EP1/EP2 endpoints (Phase 3 complete).
//
// Creates a full Matter node with three endpoints:
//   EP0 Root Node  (standard, created automatically by node::create())
//   EP1 Occupancy Sensor (0x0107) ← room_state.occupancy
//   EP2 Mode Select (0x0027) ← room_state.user_mode
//
// No EP3 in v1. Env alert is LOCAL ONLY (matter-data-model.md §5).
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

#include "esp_bt.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <app/clusters/mode-select-server/supported-modes-manager.h>

#include "state_machine.h"   // g_matter_report_queue, g_app_event_queue, matter_report_t, app_event_t

using namespace esp_matter;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

static const char *TAG = "matter_app";

// Endpoint handles (matter-data-model.md §2, v0.3).
// EP0 is created automatically by node::create().
static node_t     *s_node             = nullptr;
static endpoint_t *s_ep1_occupancy    = nullptr;   // Occupancy Sensor (0x0107)
static endpoint_t *s_ep2_mode_select  = nullptr;   // Mode Select (0x0027)

// Cached endpoint IDs for attribute updates in matter_adapter_task.
static uint16_t s_ep1_id = 0;
static uint16_t s_ep2_id = 0;

// Supported modes for EP2 ModeSelect (matter-data-model.md §4.2).
// Phase 3 Step 6: 3 modes — Normal(0), Quiet(1), Night(2).
static ModeSelect::StaticSupportedModesManager s_supported_modes;

#define MATTER_ADAPTER_QUEUE_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)

// ─── ESP-Matter callbacks ───

// Event callback: called by the ESP-Matter CHIP task for platform events.
// Phase 3 Step 3: commissioning lifecycle events are forwarded as
// EVENT_MATTER_LIFECYCLE to g_app_event_queue so state_machine_task can
// track matter_commissioned, commissioning_active, and wifi_connected.
// Drop-on-full (send timeout = 0) is acceptable because Matter lifecycle
// events are idempotent — a missed FabricRemoved, for example, will be
// reconciled when the state machine processes the next GOT_IP or
// DISCONNECTED event that updates room_state.wifi_connected.
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    matter_lifecycle_t lifecycle = { MATTER_LIFECYCLE_SESSION_STOPPED, 0 };
    bool send_lifecycle = false;

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
        lifecycle.event = MATTER_LIFECYCLE_SESSION_STOPPED;
        send_lifecycle = true;
        break;

    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "event: commissioning window opened (BLE advertising)");
        lifecycle.event = MATTER_LIFECYCLE_WINDOW_OPENED;
        send_lifecycle = true;
        break;

    case DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "event: commissioning window closed");
        lifecycle.event = MATTER_LIFECYCLE_WINDOW_CLOSED;
        send_lifecycle = true;
        break;

    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "event: commissioning complete (fabric established)");
        lifecycle.event = MATTER_LIFECYCLE_COMMISSIONING_COMPLETE;
        send_lifecycle = true;

        // Phase 3 Step 5: release BLE memory after successful commissioning.
        // Commissioning is complete, the device now operates over Wi-Fi, and
        // BLE is no longer needed. Releasing ~20-30 KB back to the heap
        // (commissioning-lifecycle.md §3.4).
        {
            esp_err_t bt_err = esp_bt_mem_release(ESP_BT_MODE_BLE);
            if (bt_err != ESP_OK) {
                ESP_LOGW(TAG, "esp_bt_mem_release(BLE): %s (non-fatal)",
                         esp_err_to_name(bt_err));
            } else {
                ESP_LOGI(TAG, "BLE memory released (~20-30 KB)");
            }
        }
        break;

    case DeviceEventType::kFabricRemoved: {
        // Phase 3 Step 5: check remaining fabric count before deciding
        // whether to clear matter_commissioned. In multi-admin scenarios,
        // only one of several fabrics may be removed — the device is still
        // commissioned if at least one fabric remains.
        ESP_LOGI(TAG, "event: fabric removed (checking remaining fabric count)");

        uint8_t remaining = 0;
        // FabricTable is safe to query here — we're on the CHIP task context
        // and the fabric was already removed from the table.
        auto &server = chip::Server::GetInstance();
        auto &fabricTable = server.GetFabricTable();
        remaining = fabricTable.FabricCount();
        ESP_LOGI(TAG, "fabrics remaining after removal: %u", (unsigned)remaining);

        lifecycle.event = MATTER_LIFECYCLE_FABRIC_REMOVED;
        lifecycle.remaining_fabrics = remaining;
        send_lifecycle = true;
        break;
    }

    default:
        break;
    }

    if (send_lifecycle) {
        app_event_t ev = {
            .type = EVENT_MATTER_LIFECYCLE,
            .data.matter_lifecycle = lifecycle,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        if (xQueueSend(g_app_event_queue, &ev, 0) != pdTRUE) {
            ESP_LOGW(TAG, "lifecycle event %d dropped (app_event_queue full)",
                     (int)lifecycle.event);
        }
    }
}

// Attribute update callback: called when a controller writes an attribute.
// Phase 3 Step 6: handle ChangeToMode on EP2 ModeSelect → app_event_queue.
// EP0 root node attributes are handled internally by ESP-Matter.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id,
                                         uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    (void)priv_data;

    // Only handle POST_WRITE on EP2 ModeSelect::CurrentMode (ChangeToMode).
    if (type != attribute::callback_type_t::PRE_ATTRIBUTE_CHANGE ||
        endpoint_id != s_ep2_id ||
        cluster_id != ModeSelect::Id ||
        attribute_id != ModeSelect::Attributes::CurrentMode::Id) {
        return ESP_OK;
    }

    // The CHIP SDK validates the mode against SupportedModes before calling
    // this callback, so val->val.u8 is guaranteed to be a valid mode (0-2).
    uint8_t new_mode = val->val.u8;
    ESP_LOGI(TAG, "ChangeToMode: controller requests mode %u", (unsigned)new_mode);

    // Forward to state_machine_task for validation against local constraints
    // (NIGHT window, etc.) and execution.
    app_event_t ev = {
        .type = EVENT_MATTER_COMMAND,
        .data.matter_cmd = {
            .type     = MATTER_COMMAND_CHANGE_TO_MODE,
            .new_mode = new_mode,
            .cmd_ctx  = nullptr,   // Phase 3 Step 6: cmd_ctx is the callback
                                   // itself; we respond inline from the CHIP
                                   // context via matter_adapter_task.
        },
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
    if (xQueueSend(g_app_event_queue, &ev, 0) != pdTRUE) {
        ESP_LOGE(TAG, "ChangeToMode event dropped (queue full) — command ignored");
        return ESP_FAIL;
    }
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

    // ── Step 1: create EP0 Root Node ──
    // node::create() internally adds the root node device type to endpoint 0.
    node::config_t node_config;
    s_node = node::create(&node_config,
                          app_attribute_update_cb,
                          app_identification_cb);
    if (s_node == nullptr) {
        ESP_LOGE(TAG, "node::create failed");
        return ESP_FAIL;
    }

    // ── Step 2: create EP1 — Occupancy Sensor (device type 0x0107) ──
    // matter-data-model.md §3: OccupancySensing cluster, PIR sensor type.
    {
        endpoint::occupancy_sensor::config_t occ_cfg;
        occ_cfg.occupancy_sensing.occupancy_sensor_type =
            chip::to_underlying(OccupancySensing::OccupancySensorTypeEnum::kPir);
        occ_cfg.occupancy_sensing.occupancy_sensor_type_bitmap =
            chip::to_underlying(OccupancySensing::OccupancySensorTypeBitmap::kPir);

        s_ep1_occupancy = endpoint::occupancy_sensor::create(
            s_node, &occ_cfg, ENDPOINT_FLAG_NONE, nullptr);
        if (s_ep1_occupancy == nullptr) {
            ESP_LOGE(TAG, "EP1 occupancy_sensor::create failed");
            return ESP_FAIL;
        }
        s_ep1_id = endpoint::get_id(s_ep1_occupancy);
        ESP_LOGI(TAG, "EP1 Occupancy Sensor created (endpoint %u, PIR type)",
                 (unsigned)s_ep1_id);
    }

    // ── Step 3: create EP2 — Mode Select (device type 0x0027) ──
    // matter-data-model.md §4: ModeSelect cluster, 3 modes (Normal/Quiet/Night).
    {
        endpoint::mode_select::config_t mode_cfg;
        memcpy(mode_cfg.mode_select.description, "Room Mode", 10);

        s_ep2_mode_select = endpoint::mode_select::create(
            s_node, &mode_cfg, ENDPOINT_FLAG_NONE, nullptr);
        if (s_ep2_mode_select == nullptr) {
            ESP_LOGE(TAG, "EP2 mode_select::create failed");
            return ESP_FAIL;
        }
        s_ep2_id = endpoint::get_id(s_ep2_mode_select);

        // Register the 3 supported modes.
        // TODO: supported modes should be configurable per matter-data-model.md §4.2.
        //       Hardcoding here is Phase 3 Step 6 baseline; future iteration should
        //       populate from a configurable source.
        s_supported_modes.AddMode(ModeSelect::ModeOptionStruct{
            .label = chip::CharSpan("Normal", 6),
            .mode = 0,
            .modeTags = chip::Span<const ModeSelect::ModeTagStruct>(),
        });
        s_supported_modes.AddMode(ModeSelect::ModeOptionStruct{
            .label = chip::CharSpan("Quiet", 5),
            .mode = 1,
            .modeTags = chip::Span<const ModeSelect::ModeTagStruct>(),
        });
        s_supported_modes.AddMode(ModeSelect::ModeOptionStruct{
            .label = chip::CharSpan("Night", 5),
            .mode = 2,
            .modeTags = chip::Span<const ModeSelect::ModeTagStruct>(),
        });

        ESP_LOGI(TAG, "EP2 Mode Select created (endpoint %u, 3 modes)",
                 (unsigned)s_ep2_id);
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
    ESP_LOGI(TAG, "Matter stack started (EP0+EP1+EP2, BLE commissioning ready)");
    return ESP_OK;
}

esp_err_t matter_app_respond_change_to_mode(void *cmd_ctx, bool success)
{
    (void)cmd_ctx;
    // Phase 3 Step 6: the CHIP stack handles the ChangeToMode response
    // automatically through the attribute::callback_type_t return value
    // in app_attribute_update_cb. Accepting the mode there (returning ESP_OK)
    // already tells the controller the write succeeded. This function is
    // retained for ABI compatibility with matter_app.h; state_machine_task
    // processes the mode change asynchronously and pushes a MATTER_REPORT
    // via g_matter_report_queue.
    (void)success;
    return ESP_OK;
}

void matter_adapter_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d, EP1=%u, EP2=%u)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (int)uxTaskPriorityGet(NULL),
             (unsigned)s_ep1_id, (unsigned)s_ep2_id);

    for (;;) {
        matter_report_t report;
        if (xQueueReceive(g_matter_report_queue, &report,
                          pdMS_TO_TICKS(MATTER_ADAPTER_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            // Phase 3 Step 6: apply reports to EP1/EP2 Matter attributes.
            // All attribute::update calls MUST be under the CHIP stack lock
            // (task-architecture.md §4.7, §6.2). ScopedChipStackLock is RAII
            // and releases automatically when the scope exits.

            uint8_t occ_val;
            bool should_update_occupancy = false;
            bool should_update_mode = false;

            switch (report.type) {
            case MATTER_REPORT_OCCUPANCY:
                if (report.occupancy != OCCUPANCY_UNKNOWN) {
                    occ_val = (report.occupancy == OCCUPANCY_OCCUPIED) ? 1 : 0;
                    should_update_occupancy = true;
                }
                break;

            case MATTER_REPORT_CURRENT_MODE:
                should_update_mode = true;
                break;

            case MATTER_REPORT_FORCE_SYNC:
                // Wi-Fi reconnected — re-report both attributes.
                if (report.occupancy != OCCUPANCY_UNKNOWN) {
                    occ_val = (report.occupancy == OCCUPANCY_OCCUPIED) ? 1 : 0;
                    should_update_occupancy = true;
                }
                should_update_mode = true;
                break;
            }

            if (should_update_occupancy || should_update_mode) {
                esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);

                if (should_update_occupancy) {
                    esp_matter_attr_val_t val = esp_matter_uint8(occ_val);
                    attribute::update(s_ep1_id,
                                      OccupancySensing::Id,
                                      OccupancySensing::Attributes::Occupancy::Id,
                                      &val);
                    ESP_LOGD(TAG, "EP1 occupancy → %u", (unsigned)occ_val);
                }

                if (should_update_mode) {
                    esp_matter_attr_val_t val =
                        esp_matter_uint8((uint8_t)report.user_mode);
                    attribute::update(s_ep2_id,
                                      ModeSelect::Id,
                                      ModeSelect::Attributes::CurrentMode::Id,
                                      &val);
                    ESP_LOGD(TAG, "EP2 CurrentMode → %u",
                             (unsigned)report.user_mode);
                }
            }
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}

// --- Factory reset (Phase 3 Step 4) ---
// Commissioning-lifecycle.md §5: full factory reset triggered by confirmed
// long-press (5 s). Erases the default NVS partition (Wi-Fi/Matter/BLE
// credentials), calls esp_wifi_restore() to clear Wi-Fi config from the
// PHY, and reboots. The caller MUST have already called
// config_factory_reset() to erase the business config (ps_cfg partition)
// BEFORE calling this function.
//
// After the default NVS erase, the device has no Wi-Fi credentials, no
// Matter fabric, and no BLE commissioning state. On the next boot it will
// enter fresh BLE commissioning mode (commissioning-lifecycle.md §3.1).
//
// SECURITY: this function does NOT log or persist any credentials. All
// sensitive data is erased by nvs_flash_erase().
void matter_app_factory_reset(void)
{
    ESP_LOGI(TAG, "=== FACTORY RESET: erasing default NVS partition ===");

    esp_err_t ret = nvs_flash_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "nvs_flash_deinit: %s — proceeding with erase anyway",
                 esp_err_to_name(ret));
    }

    ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase: %s — NVS may not be fully erased, "
                 "but proceeding with reboot", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "default NVS partition erased (Wi-Fi/Matter/BLE "
                 "credentials cleared)");
    }

    // Clear Wi-Fi configuration from the PHY. This ensures the radio
    // does not try to auto-connect with stale credentials on next boot.
    esp_wifi_restore();
    ESP_LOGI(TAG, "Wi-Fi config restored to factory defaults");

    // Short delay so the green LED confirmation is visible (commissioning-
    // lifecycle.md §5.2 step 4: "绿色常亮 1 s → 系统重启"). The LED was
    // set by the caller (state_machine_task) before invoking this function.
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "rebooting...");
    esp_restart();
}
