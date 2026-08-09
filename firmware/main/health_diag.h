// PrivacySense Matter Room Hub - bounded health diagnostics
//
// The pure classification helpers in this header are intentionally free of
// ESP-IDF dependencies so that their boundary behavior can be Host-tested.
// The target-only collection/logging API is enabled by ESP_PLATFORM.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This is a diagnostic-only log threshold. It does not trigger a reset,
// safe-mode transition, allocation failure policy, or any other mitigation.
#define HEALTH_DIAG_HEAP_WARN_BYTES  (16U * 1024U)

// The task snapshot is deliberately fixed-size. If the target has more tasks
// than this bound, the diagnostic reports the truncation instead of growing a
// buffer or allocating at runtime.
#define HEALTH_DIAG_MAX_TASKS  32U

typedef enum {
    HEALTH_DIAG_RESET_NORMAL = 0,
    HEALTH_DIAG_RESET_SOFTWARE,
    HEALTH_DIAG_RESET_PANIC,
    HEALTH_DIAG_RESET_WATCHDOG,
    HEALTH_DIAG_RESET_BROWNOUT,
    HEALTH_DIAG_RESET_UNKNOWN,
} health_diag_reset_class_t;

typedef enum {
    HEALTH_DIAG_HEAP_OK = 0,
    HEALTH_DIAG_HEAP_LOW,
} health_diag_heap_class_t;

// ESP-IDF esp_reset_reason_t values used by the v5.x baseline. Keeping the
// numeric mapping in this pure helper lets Host tests cover reset reporting
// without pulling the target SDK into the Host test binary.
enum {
    HEALTH_DIAG_RST_UNKNOWN   = 0,
    HEALTH_DIAG_RST_POWERON   = 1,
    HEALTH_DIAG_RST_EXT       = 2,
    HEALTH_DIAG_RST_SW        = 3,
    HEALTH_DIAG_RST_PANIC     = 4,
    HEALTH_DIAG_RST_INT_WDT   = 5,
    HEALTH_DIAG_RST_TASK_WDT  = 6,
    HEALTH_DIAG_RST_WDT       = 7,
    HEALTH_DIAG_RST_DEEPSLEEP = 8,
    HEALTH_DIAG_RST_BROWNOUT = 9,
    HEALTH_DIAG_RST_SDIO      = 10,
};

static inline health_diag_reset_class_t
health_diag_classify_reset_reason(int reason_code)
{
    switch (reason_code) {
    case HEALTH_DIAG_RST_POWERON:
    case HEALTH_DIAG_RST_EXT:
    case HEALTH_DIAG_RST_DEEPSLEEP:
    case HEALTH_DIAG_RST_SDIO:
        return HEALTH_DIAG_RESET_NORMAL;
    case HEALTH_DIAG_RST_SW:
        return HEALTH_DIAG_RESET_SOFTWARE;
    case HEALTH_DIAG_RST_PANIC:
        return HEALTH_DIAG_RESET_PANIC;
    case HEALTH_DIAG_RST_INT_WDT:
    case HEALTH_DIAG_RST_TASK_WDT:
    case HEALTH_DIAG_RST_WDT:
        return HEALTH_DIAG_RESET_WATCHDOG;
    case HEALTH_DIAG_RST_BROWNOUT:
        return HEALTH_DIAG_RESET_BROWNOUT;
    case HEALTH_DIAG_RST_UNKNOWN:
    default:
        return HEALTH_DIAG_RESET_UNKNOWN;
    }
}

static inline const char *
health_diag_reset_class_name(health_diag_reset_class_t reset_class)
{
    switch (reset_class) {
    case HEALTH_DIAG_RESET_NORMAL:    return "normal";
    case HEALTH_DIAG_RESET_SOFTWARE:  return "software";
    case HEALTH_DIAG_RESET_PANIC:     return "panic";
    case HEALTH_DIAG_RESET_WATCHDOG:  return "watchdog";
    case HEALTH_DIAG_RESET_BROWNOUT:  return "brownout";
    case HEALTH_DIAG_RESET_UNKNOWN:
    default:                          return "unknown";
    }
}

static inline health_diag_heap_class_t
health_diag_classify_heap(uint32_t minimum_free_heap_bytes)
{
    return (minimum_free_heap_bytes < HEALTH_DIAG_HEAP_WARN_BYTES)
               ? HEALTH_DIAG_HEAP_LOW
               : HEALTH_DIAG_HEAP_OK;
}

static inline const char *
health_diag_heap_class_name(health_diag_heap_class_t heap_class)
{
    return heap_class == HEALTH_DIAG_HEAP_LOW ? "low" : "ok";
}

#ifdef ESP_PLATFORM

#include "esp_err.h"

typedef struct {
    uint32_t free_heap_bytes;
    uint32_t minimum_free_heap_bytes;
    uint32_t task_count;
    uint32_t captured_task_count;
    uint32_t minimum_stack_high_water_mark_bytes;
    int reset_reason_code;
    health_diag_reset_class_t reset_class;
    health_diag_heap_class_t heap_class;
    bool task_list_truncated;
} health_diag_snapshot_t;

// Captures bounded heap/reset/task information without allocating or changing
// any task/watchdog policy. This function is not ISR-safe and is intended to
// be called by app_main and the existing network task only.
esp_err_t health_diag_capture(health_diag_snapshot_t *out);

// Captures and emits a sanitized, bounded diagnostic snapshot. `reason` is a
// local label such as "boot", "tasks_ready", or "network_periodic"; it must
// not contain user/network credentials or raw addresses.
void health_diag_log(const char *reason);

#endif // ESP_PLATFORM

#ifdef __cplusplus
}
#endif
