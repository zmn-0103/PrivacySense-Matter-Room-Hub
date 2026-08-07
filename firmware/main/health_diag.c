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

#define HEALTH_DIAG_TASK_NAME_BYTES  16U

typedef struct {
    char name[HEALTH_DIAG_TASK_NAME_BYTES];
    uint32_t stack_high_water_mark_bytes;
    uint32_t priority;
} health_diag_task_record_t;

// Static storage keeps the diagnostic path from adding a large temporary
// object to the network task stack. uxTaskGetSystemState() is bounded by the
// array size and is only called at boot and on the existing 30 s diagnostic
// cadence.
#if (configUSE_TRACE_FACILITY == 1)
static TaskStatus_t s_task_status[HEALTH_DIAG_MAX_TASKS];
static health_diag_task_record_t s_task_records[HEALTH_DIAG_MAX_TASKS];

static void copy_task_name(char *destination, size_t destination_size,
                           const char *source)
{
    if (destination_size == 0U) return;

    size_t i = 0U;
    if (source != NULL) {
        for (; (i + 1U) < destination_size && source[i] != '\0'; ++i) {
            destination[i] = source[i];
        }
    }
    destination[i] = '\0';
}
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
    // Keep the scheduler suspended while copying pcTaskName. The pointer
    // belongs to the task control block and must not be used after a task can
    // be deleted. uxTaskGetSystemState() returns zero when the fixed array is
    // too small; retain the real task_count so the log exposes that gap.
    vTaskSuspendAll();
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    UBaseType_t captured_count = uxTaskGetSystemState(
        s_task_status, HEALTH_DIAG_MAX_TASKS, NULL);

    if (captured_count > HEALTH_DIAG_MAX_TASKS) {
        captured_count = HEALTH_DIAG_MAX_TASKS;
    }
    for (UBaseType_t i = 0; i < captured_count; ++i) {
        copy_task_name(s_task_records[i].name,
                       sizeof(s_task_records[i].name),
                       s_task_status[i].pcTaskName);
        // FreeRTOS reports this value as unused StackType_t words. Convert
        // while the scheduler is still suspended so the stored diagnostic
        // record and its later log do not depend on the TaskStatus_t object.
        s_task_records[i].stack_high_water_mark_bytes =
            (uint32_t)s_task_status[i].usStackHighWaterMark *
            (uint32_t)sizeof(StackType_t);
        s_task_records[i].priority =
            (uint32_t)s_task_status[i].uxCurrentPriority;
    }
    (void)xTaskResumeAll();

    out->task_count = (uint32_t)task_count;
    out->captured_task_count = (uint32_t)captured_count;
    out->task_list_truncated =
        (task_count > HEALTH_DIAG_MAX_TASKS) ||
        (captured_count != task_count);

    // Keep the converted byte value as a summary while health_diag_log()
    // emits each bounded task record too.
    for (UBaseType_t i = 0; i < captured_count; ++i) {
        uint32_t high_water_mark =
            s_task_records[i].stack_high_water_mark_bytes;
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
             " captured=%" PRIu32 " capacity=%u truncated=%s"
             " min_stack_hwm=%" PRIu32 " bytes",
             reason, snapshot.reset_reason_code,
             health_diag_reset_class_name(snapshot.reset_class),
             snapshot.free_heap_bytes, snapshot.minimum_free_heap_bytes,
             health_diag_heap_class_name(snapshot.heap_class),
             snapshot.task_count, snapshot.captured_task_count,
             (unsigned)HEALTH_DIAG_MAX_TASKS,
             snapshot.task_list_truncated ? "yes" : "no",
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
        ESP_LOGI(TAG, "task=%s stack_hwm=%" PRIu32 " bytes priority=%u",
                 s_task_records[i].name,
                 s_task_records[i].stack_high_water_mark_bytes,
                 (unsigned)s_task_records[i].priority);
    }
#endif
}
