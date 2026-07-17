// PrivacySense Matter Room Hub - matter_app.c
//
// esp_matter integration. Skeleton implementation; endpoint creation and
// attribute wiring arrive in a follow-up commit after first build succeeds.
//
// Endpoint topology (matter-data-model.md §2, v0.2):
//   EP0 Root Node
//   EP1 Occupancy Sensor  device type 0x0107  (OccupancySensing 0x0406)
//   EP2 Mode Select       device type 0x0027  (ModeSelect          0x0050)
// No EP3 in v1 — env_alert is local-only.
//
// SECURITY: setup passcode / discriminator / SPAKE2+ verifier come from a
// Commissionable Data Provider per commissioning-lifecycle.md §3.2 — they are
// NOT "randomly generated on first boot". v1 options:
//   - Test Commissionable Data Provider (fixed passcode/discriminator, dev only)
//   - Factory Data Provider (pre-provisioned in NVS/Flash factory partition)
// The QR / manual-code setup payload is derived from these values and presented
// OUT-OF-BAND for the user to scan / type into the controller (e.g. printed on
// a label, shown on a temporary debug console). BLE then carries the PASE
// commissioning session that uses the passcode — the QR/manual code itself is
// never sent over BLE. We MUST NOT:
//   - Hardcode passcode/discriminator in source or sdkconfig.defaults.
//   - Log the QR code at INFO level (use ESP_LOG_DEBUG, gated by menuconfig).
//   - Persist any of these in tests/evidence/ Markdown files.
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

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_matter.h"
#include "esp_matter_endpoint.h"

#include "state_machine.h"   // g_matter_report_queue, matter_report_t, app_event_t

static const char *TAG = "matter_app";

// Endpoint handles (matter-data-model.md §2, v0.2).
// Only EP0/EP1/EP2 are created in v1. EP3 / BooleanState / stateValue are
// NOT used — env_alert is local-only.
static uint16_t s_endpoint_root      = 0;
static uint16_t s_endpoint_occupancy = 1;   // Occupancy Sensor, device type 0x0107
static uint16_t s_endpoint_mode      = 2;   // Mode Select,        device type 0x0027

#define MATTER_ADAPTER_QUEUE_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)

esp_err_t matter_app_init(void)
{
    // STUB: no esp_matter node / endpoint is created and no commissioning
    // session is started in this build. The function returns a non-OK status
    // so callers cannot mistake it for a successful Matter bring-up; main.c
    // treats ESP_ERR_NOT_SUPPORTED from this function as "Matter intentionally
    // disabled, continue boot" rather than aborting.
    //
    // TODO (follow-up commit after first build succeeds):
    //   - esp_matter::node::create() + endpoint::create() for EP0/EP1/EP2
    //     (matter-data-model.md §2):
    //       EP0 Root Node
    //       EP1 OccupancySensing device type 0x0107, cluster 0x0406
    //       EP2 ModeSelect         device type 0x0027, cluster 0x0050
    //         SupportedModes = {Normal=0, Quiet=1, Night=2}
    //   - Register esp_matter attribute-update + command callbacks:
    //       ChangeToMode on EP2 → app_event_t(EVENT_MATTER_COMMAND,
    //         matter_cmd.type=MATTER_COMMAND_CHANGE_TO_MODE) → g_app_event_queue
    //         (callback context MUST be non-blocking: queue full → respond BUSY,
    //          task-architecture.md §5.3, §4.7).
    //   - Enable esp_matter Wi-Fi provisioning; QR/manual-code setup payload
    //     is presented OUT-OF-BAND (e.g. on a label or debug console) for the
    //     user to scan/type into the controller; BLE then carries the PASE
    //     commissioning session (commissioning-lifecycle.md §3.2). NEVER log
    //     the QR payload at INFO — use ESP_LOG_DEBUG, gated by menuconfig.
    //   - Select Commissionable Data Provider (Test for dev, Factory for prod)
    //     per commissioning-lifecycle.md §3.2.

    ESP_LOGW(TAG, "STUB: Matter not yet functional — no node/endpoint created, "
             "no commissioning started (matter-data-model.md §2 TODO)");
    (void)s_endpoint_root;
    (void)s_endpoint_occupancy;
    (void)s_endpoint_mode;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t matter_app_respond_change_to_mode(void *cmd_ctx, bool success)
{
    if (cmd_ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // STUB: nothing is actually queued — there is no esp_matter endpoint to
    // respond to. Return ESP_ERR_NOT_SUPPORTED so the caller (state_machine_task)
    // can log and continue without pretending the response was delivered.
    //
    // TODO (follow-up commit after first build succeeds):
    //   - Defer the actual esp_matter command-response call to the CHIP stack
    //     context (e.g. by enqueuing a small item processed by
    //     matter_adapter_task). Calling esp_matter from state_machine_task
    //     context would violate the Matter threading model.
    //   - Map `success` to the Matter StatusIB:
    //       success == true  → Status = Success (0x00)
    //       success == false → Status = Failure (0x01) with appropriate
    //                          cluster-specific status code for NIGHT rejected
    //                          (e.g. ConstraintError, matter-data-model.md §4.6).
    ESP_LOGW(TAG, "STUB: ChangeToMode response NOT sent (ctx=%p, success=%d) — "
             "no esp_matter endpoint wired yet",
             cmd_ctx, success ? 1 : 0);
    (void)success;
    return ESP_ERR_NOT_SUPPORTED;
}

void matter_adapter_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), uxTaskPriorityGet(NULL));

    for (;;) {
        matter_report_t report;
        // Block up to 2 s waiting for a report; the timeout also serves as
        // the TWDT feed cadence (task-architecture.md §7.2).
        if (xQueueReceive(g_matter_report_queue, &report,
                          pdMS_TO_TICKS(MATTER_ADAPTER_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            // TODO: apply the report to Matter attributes under the CHIP
            //   stack lock. Mapping (matter-data-model.md §3, §4, §6):
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
            // Respect min-report-interval limits (matter-data-model.md §6.2):
            //   occupancy:   1 s
            //   currentMode: no limit
            ESP_LOGD(TAG, "report type=%d (apply TODO)", (int)report.type);
        }
        // Timeout with no report → fall through to TWDT feed. This is the
        // 2 s health-check heartbeat required by task-architecture.md §7.2.

        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}
