// PrivacySense Matter Room Hub - matter_app.h
//
// esp_matter integration. Creates the Matter endpoint tree and bridges
// local room_state to Matter attributes via the matter_report_queue.
//
// Endpoint topology (matter-data-model.md §2, v0.2):
//   EP0 Root Node                 (standard, required)
//   EP1 Occupancy Sensor (0x0107) ← room_state.occupancy
//   EP2 Mode Select       (0x0027) ← room_state.user_mode
//
// No EP3 in v1. Env alert is LOCAL ONLY (matter-data-model.md §5).
//
// Event flow (task-architecture.md §3, §4.7, §5.1):
//   ESP-Matter CHIP callbacks (ChangeToMode, attribute read)
//     → matter_adapter_task builds app_event_t(EVENT_MATTER_*)
//     → sends to g_app_event_queue (consumed by state_machine_task)
//   state_machine_task (on occupancy / user_mode transition)
//     → sends matter_report_t to g_matter_report_queue
//     → matter_adapter_task consumes it and updates Matter attributes
//       under the CHIP stack lock
//   state_machine_task (after ChangeToMode evaluation, ≤ 100 ms)
//     → calls matter_app_respond_change_to_mode() with SUCCESS / FAILURE
//
// SECURITY: setup passcode / discriminator / SPAKE2+ verifier are NOT
// hardcoded. They come from a Commissionable Data Provider at runtime
// (commissioning-lifecycle.md §3.2):
//   - Test Provider  (fixed values, dev only) for first-build bring-up
//   - Factory Provider (pre-provisioned in factory NVS/Flash) for production
// The QR / manual-code setup payload is derived from these values and presented
// OUT-OF-BAND for the user to scan / type into the controller (e.g. printed on
// a label). BLE then carries the PASE commissioning session that uses the
// passcode — the QR/manual code itself is never sent over BLE. It is NOT
// "randomly generated on first boot" and MUST NOT be logged at INFO — only
// ESP_LOG_DEBUG, gated by menuconfig, and never captured into the repository
// or test evidence files.

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Lifecycle ---
// Initialise the Matter stack: create the minimal node (EP0 Root Node only)
// and start esp_matter (spawns the internal CHIP task, initialises BLE
// NimBLE, opens the commissioning window, begins advertising).
//
// Current contract (Phase 3 Step 6):
//   - Creates EP0 (Root Node), EP1 (OccupancySensor, 0x0107), EP2
//     (ModeSelect, 0x0027) with 3 supported modes (Normal/Quiet/Night).
//   - app_attribute_update_cb routes ChangeToMode commands to
//     state_machine_task via g_app_event_queue.
//   - matter_adapter_task applies MATTER_REPORT_OCCUPANCY/CURRENT_MODE/
//     FORCE_SYNC to EP1/EP2 attributes under the CHIP stack lock.
//   - Returns ESP_OK on success, ESP_FAIL on failure.
//   - Does NOT spawn matter_adapter_task — that is done by app_main so the
//     task parameters live in one place.
//   - Callers MUST NOT abort on failure — main.c enters a Matter-degraded
//     state (local sensors / state machine / UI continue without Matter).
esp_err_t matter_app_init(void);

// matter_adapter_task entry point. Created by app_main with stack 12288,
// prio 4 (task-architecture.md §4.7). Consumes g_matter_report_queue and
// applies attribute updates on the CHIP stack context. 2 s queue wait
// timeout for TWDT feed + health check.
void matter_adapter_task(void *pvParameters);

// Called by state_machine_task after evaluating a ChangeToMode command
// (matter-data-model.md §4.4-4.6). MUST be invoked within 100 ms of
// receiving the command (task-architecture.md §4.7).
//
// `cmd_ctx`       Opaque pointer from the originating Matter callback
//                 (forwarded unchanged through app_event_t.matter_cmd.cmd_ctx).
// `success`       true  → return SUCCESS to the controller
//                 false → return FAILURE to the controller
//
// Implementations must defer the actual esp_matter command response to the
// CHIP stack task (e.g. via a queued item processed by matter_adapter_task),
// not call into esp_matter from the state_machine_task context.
//
// Current contract (Phase 3 Step 6): EP2 ModeSelect is now created, so
// ChangeToMode is handled by the CHIP stack via app_attribute_update_cb.
// state_machine_task processes the mode change asynchronously; this function
// is retained for ABI compatibility and always returns ESP_OK.
esp_err_t matter_app_respond_change_to_mode(void *cmd_ctx, bool success);

// --- Factory reset (Phase 3 Step 4) ---
// Scheduled by state_machine_task after a confirmed long-press.
// Erases the default NVS partition (Wi-Fi/Matter/BLE credentials),
// calls esp_wifi_restore() to clear Wi-Fi config, and reboots after
// 1 s to allow the green LED confirmation to be visible.
//
// Caller MUST have already called config_factory_reset() to erase
// the business config (ps_cfg partition) BEFORE calling this function.
// After this function is called, the system WILL reboot — there is
// no return path for error handling after the reboot is scheduled.
void matter_app_factory_reset(void);

#ifdef __cplusplus
}
#endif
