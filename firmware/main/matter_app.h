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
// Initialise esp_matter node + EP0/EP1/EP2 endpoints, register attribute
// update and command callbacks. Does NOT spawn matter_adapter_task — that
// is done by app_main so the task parameters live in one place.
//
// STUB CONTRACT (current build): returns ESP_ERR_NOT_SUPPORTED — no node /
// endpoint is created and no commissioning is started. Callers MUST treat
// ESP_ERR_NOT_SUPPORTED as "Matter intentionally disabled, continue boot"
// (see main.c) rather than aborting. Any other non-OK return is a real
// failure. Will return ESP_OK once the matter-data-model.md §2 endpoints
// are wired in a follow-up commit.
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
// STUB CONTRACT (current build): returns ESP_ERR_NOT_SUPPORTED — nothing is
// queued and no response is sent to any controller. Callers MUST log the
// drop and continue (the state machine must not block on a Matter response
// that will never arrive). Will return ESP_OK once the endpoint tree from
// matter-data-model.md §2 is wired.
esp_err_t matter_app_respond_change_to_mode(void *cmd_ctx, bool success);

#ifdef __cplusplus
}
#endif
