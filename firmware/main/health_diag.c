// PrivacySense Matter Room Hub - bounded health diagnostics

#include "health_diag.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "health_diag";

// Static storage keeps the diagnostic path from adding a large temporary
// object to the network task stack. uxTaskGetSystemState() is bounded by the
// array size and is only called at boot and on the existing 30 s diagnostic
// cadence.
#if (configUSE_TRACE_FACILITY == 1)
static TaskStatus_t s_task_status[HEALTH_DIAG_MAX_TASKS];
#endif

esp_err_t health_diag_capture(health_diag_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    out->minimum_stack_high_water_mark_bytes = UINT32_MAX;
    out->free_heap_bytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    out->minimum_free_heap_bytes =
        (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    out->heap_class = health_diag_classify_heap(out->minimum_free_heap_bytes);
    out->reset_reason_code = (int)esp_reset_reason();
    out->reset_class =
        health_diag_classify_reset_reason(out->reset_reason_code);

#if (configUSE_TRACE_FACILITY == 1)
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    UBaseType_t captured_count = uxTaskGetSystemState(
        s_task_status, HEALTH_DIAG_MAX_TASKS, NULL);

    out->task_count = (uint32_t)task_count;
    out->captured_task_count = (uint32_t)captured_count;
    out->task_list_truncated =
        (task_count > HEALTH_DIAG_MAX_TASKS) ||
        (captured_count != task_count);

    // ESP-IDF reports this high-water mark in bytes. Keep the minimum as a
    // summary while health_diag_log() emits each bounded task record too.
    for (UBaseType_t i = 0; i < captured_count; ++i) {
        uint32_t high_water_mark =
            (uint32_t)s_task_status[i].usStackHighWaterMark;
        if (high_water_mark < out->minimum_stack_high_water_mark_bytes) {
            out->minimum_stack_high_water_mark_bytes = high_water_mark;
        }
    }
#else
    // sdkconfig.defaults enables trace support. Keep a truthful explicit gap
    // if a future configuration disables it; heap/reset diagnostics remain
    // available and no fallback silently claims task coverage.
    out->task_list_truncated = true;
#endif

    return ESP_OK;
}

void health_diag_log(const char *reason)
{
    health_diag_snapshot_t snapshot;
    if (health_diag_capture(&snapshot) != ESP_OK) {
        ESP_LOGE(TAG, "capture failed");
        return;
    }

    if (reason == NULL) reason = "unspecified";

    ESP_LOGI(TAG,
             "snapshot=%s reset_reason=%d reset_class=%s free_heap=%" PRIu32
             " min_free_heap=%" PRIu32 " heap=%s tasks=%" PRIu32
             " captured=%" PRIu32 " min_stack_hwm=%" PRIu32 " bytes",
             reason, snapshot.reset_reason_code,
             health_diag_reset_class_name(snapshot.reset_class),
             snapshot.free_heap_bytes, snapshot.minimum_free_heap_bytes,
             health_diag_heap_class_name(snapshot.heap_class),
             snapshot.task_count, snapshot.captured_task_count,
             snapshot.minimum_stack_high_water_mark_bytes == UINT32_MAX
                 ? 0U
                 : snapshot.minimum_stack_high_water_mark_bytes);

    if (snapshot.heap_class == HEALTH_DIAG_HEAP_LOW) {
        ESP_LOGW(TAG, "minimum free heap below diagnostic threshold (%u bytes)",
                 (unsigned)HEALTH_DIAG_HEAP_WARN_BYTES);
    }

    if (snapshot.task_list_truncated) {
        ESP_LOGW(TAG,
                 "task snapshot truncated (capacity=%u; no task allocation performed)",
                 (unsigned)HEALTH_DIAG_MAX_TASKS);
    }

#if (configUSE_TRACE_FACILITY == 1)
    for (uint32_t i = 0; i < snapshot.captured_task_count; ++i) {
        const char *name = s_task_status[i].pcTaskName;
        if (name == NULL) name = "?";
        ESP_LOGI(TAG, "task=%s stack_hwm=%" PRIu32 " bytes priority=%u",
                 name, (uint32_t)s_task_status[i].usStackHighWaterMark,
                 (unsigned)s_task_status[i].uxCurrentPriority);
    }
#endif
}
