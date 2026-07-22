// PrivacySense Matter Room Hub - config.c
//
// NVS-backed configuration loader. Skeleton implementation.
// Defaults mirror docs/state-model.md §8. Schema version 1.
//
// Task contract (task-architecture.md §4.8, §7.2):
//   - config_task is the ONLY writer to NVS (single-owner resource).
//   - 2 s queue wait timeout → feed TWDT. NVS writes are bounded to 5 s
//     (task-architecture.md §4.8); if a write exceeds 2 s the next TWDT feed
//     is delayed, but the 10 s TWDT timeout still leaves headroom.

#include "config.h"

#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "config";

QueueHandle_t g_config_event_queue  = NULL;
QueueHandle_t g_config_result_queue = NULL;

// --- Defaults (state-model.md §8) ---
static const ps_config_t s_default_cfg = {
    .entry_confirm_ms    = 2000U,
    .exit_delay_ms       = 120000U,
    .sensor_timeout_ms   = 10000U,
    .radar_eval_ms       = 200U,
    .night_start_min     = 22 * 60,   // 22:00 = 1320
    .night_end_min       = 7 * 60,    // 07:00 = 420
    .temp_alert_cc       = 3200,      // 32.00 °C (centi-celsius)
    .temp_clear_cc       = 3000,      // 30.00 °C (centi-celsius)
    .humid_alert_permil  = 750,       // 75.0 %RH
    .humid_clear_permil  = 700,       // 70.0 %RH
    .co2_alert_ppm       = 1000,
    .co2_clear_ppm       = 800,
    .alert_confirm_s     = 60,
    .alert_clear_s       = 120,
    .config_version      = CONFIG_VERSION_CURRENT,
};

static ps_config_t s_cfg;
static SemaphoreHandle_t s_cfg_mutex = NULL;   // TODO: replace with config_mutex per task-architecture.md §6.1

#define CONFIG_TASK_QUEUE_TIMEOUT_MS  2000U   // task-architecture.md §7.2 (≤ 2 s feed gap)

// Forward declarations so config_init() can use config_validate() and
// persist_candidate_to_nvs() (both defined below). The single-writer rule is
// preserved: config_init() runs before config_task is spawned, and is the
// only path that writes NVS outside config_task.
static bool      config_validate(const ps_config_t *c);
static esp_err_t persist_candidate_to_nvs(const ps_config_t *candidate);
static void      post_config_result(config_result_kind_t kind, esp_err_t err);

esp_err_t config_init(void)
{
    if (g_config_event_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_cfg_mutex = xSemaphoreCreateMutex();
    if (s_cfg_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Initialise the DEDICATED ps_cfg NVS partition. This is independent of
    // the default `nvs` partition (Wi-Fi/Matter/BLE credentials); corruption
    // of the default partition does NOT affect business config.
    esp_err_t ret = nvs_flash_init_partition(CONFIG_NVS_PARTITION);
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // ps_cfg partition corrupt — erase ONLY this partition. Business config
        // is reset to defaults; Wi-Fi/Matter/BLE credentials are untouched.
        ESP_LOGE(TAG, "ps_cfg partition corrupt (%s); erasing ps_cfg only — "
                 "business config will reset to defaults, Wi-Fi/Matter unaffected",
                 esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase_partition(CONFIG_NVS_PARTITION));
        ret = nvs_flash_init_partition(CONFIG_NVS_PARTITION);
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t h;
    ret = nvs_open_from_partition(CONFIG_NVS_PARTITION, CONFIG_NVS_NAMESPACE,
                                  NVS_READONLY, &h);
    if (ret == ESP_OK) {
        size_t len = sizeof(s_cfg);
        ret = nvs_get_blob(h, "cfg", &s_cfg, &len);
        nvs_close(h);

        if (ret != ESP_OK || len != sizeof(s_cfg)) {
            // Blob missing or wrong size — fall back to defaults. The default
            // blob will be persisted on the first CONFIG_EVENT_APPLY_NEW or
            // FACTORY_RESET; we do NOT write here to keep config_init read-only
            // on the happy path.
            ESP_LOGW(TAG, "NVS config missing/size mismatch; applying defaults");
            s_cfg = s_default_cfg;
        } else if (!config_validate(&s_cfg)) {
            // Loaded blob is wrong version, out of range, or violates
            // hysteresis invariants. config_validate checks version + ranges
            // + alert/clear relationships. Fall back to defaults AND overwrite
            // the bad blob so the next boot doesn't re-load it (avoids a
            // boot loop if the bad data was caused by a buggy writer).
            // TODO: schema migration path — currently any version mismatch
            //       is treated as "use defaults". When migrations are added,
            //       version < CONFIG_VERSION_CURRENT should be migrated
            //       in-place rather than discarded.
            ESP_LOGW(TAG, "NVS config failed validation (version=%u); "
                     "applying defaults + overwriting bad blob",
                     s_cfg.config_version);
            s_cfg = s_default_cfg;
            esp_err_t perr = persist_candidate_to_nvs(&s_default_cfg);
            if (perr != ESP_OK) {
                ESP_LOGE(TAG, "failed to overwrite bad NVS config: %s — "
                         "RAM-only defaults; next boot will retry",
                         esp_err_to_name(perr));
            }
        } else {
            ESP_LOGI(TAG, "config loaded from NVS (version=%u)",
                     s_cfg.config_version);
        }
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS namespace empty; applying defaults");
        s_cfg = s_default_cfg;
    } else {
        ESP_LOGE(TAG, "nvs_open_from_partition failed: %s", esp_err_to_name(ret));
        s_cfg = s_default_cfg;
    }

    g_config_event_queue = xQueueCreate(4, sizeof(config_event_t));
    if (g_config_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    // Result queue for async ack. Depth 2: one in-flight + one queued. If the
    // consumer is slow, config_task peeks-and-skips stale entries to make
    // room (see post_config_result). This MUST NOT block config_task.
    g_config_result_queue = xQueueCreate(2, sizeof(config_result_t));
    if (g_config_result_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// Synchronous factory reset of the business config partition (Phase 3 Step 4).
// Erases the config namespace in ps_cfg, then writes default values so the
// partition is clean AND valid on the next boot. Called by state_machine_task
// during long-press factory reset — the device is about to reboot, so this
// MUST complete before NVS is fully erased by matter_app_factory_reset().
//
// Returns:
//   ESP_OK                  — erase + defaults written
//   ESP_ERR_NVS_... / ESP_FAIL — erase or write failed; caller MUST log and
//                                 may choose to proceed with system reset anyway
esp_err_t config_factory_reset(void)
{
    ESP_LOGI(TAG, "factory reset: erasing ps_cfg partition...");

    // Deinit the partition first (nvs_flash_erase_partition requires it).
    esp_err_t ret = nvs_flash_deinit_partition(CONFIG_NVS_PARTITION);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "nvs_flash_deinit_partition(ps_cfg): %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_flash_erase_partition(CONFIG_NVS_PARTITION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase_partition(ps_cfg): %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Re-initialise the partition so we can write defaults.
    ret = nvs_flash_init_partition(CONFIG_NVS_PARTITION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init_partition(ps_cfg) after erase: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Write default config so the next boot doesn't see an empty namespace.
    esp_err_t perr = persist_candidate_to_nvs(&s_default_cfg);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "factory reset: writing defaults to ps_cfg failed: %s",
                 esp_err_to_name(perr));
        return perr;
    }

    ESP_LOGI(TAG, "factory reset: ps_cfg partition erased, defaults written");
    return ESP_OK;
}

esp_err_t config_get(ps_config_t *out)
{
    if (out == NULL || s_cfg_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_cfg_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(out, &s_cfg, sizeof(*out));
    xSemaphoreGive(s_cfg_mutex);
    return ESP_OK;
}

// Validate a candidate config against state-model.md §8 ranges.
// Checks:
//   - config_version == CONFIG_VERSION_CURRENT (caller cannot persist a
//     wrong-version config; migration is a separate TODO).
//   - Every tunable field is within its documented range.
//   - Alert/clear hysteresis: alert threshold MUST be strictly above the
//     clear threshold for temp/humid/CO2. Allowing equality would cause
//     oscillation at the threshold; inverting them would make the alert
//     condition unreachable.
// Returns true if all checks pass; false otherwise.
static bool config_validate(const ps_config_t *c)
{
    if (c->config_version != CONFIG_VERSION_CURRENT) return false;
    if (c->entry_confirm_ms < 500 || c->entry_confirm_ms > 5000) return false;
    if (c->exit_delay_ms < 30000 || c->exit_delay_ms > 600000) return false;
    if (c->sensor_timeout_ms < 5000 || c->sensor_timeout_ms > 30000) return false;
    if (c->radar_eval_ms < 100 || c->radar_eval_ms > 500) return false;
    if (c->night_start_min > 1439) return false;
    if (c->night_end_min > 1439) return false;
    if (c->temp_alert_cc < -4000 || c->temp_alert_cc > 8000) return false;
    if (c->temp_clear_cc < -4000 || c->temp_clear_cc > 8000) return false;
    if (c->humid_alert_permil > 1000) return false;
    if (c->humid_clear_permil > 1000) return false;
    if (c->co2_alert_ppm < 400 || c->co2_alert_ppm > 5000) return false;
    if (c->co2_clear_ppm < 400 || c->co2_clear_ppm > 5000) return false;
    if (c->alert_confirm_s < 10 || c->alert_confirm_s > 300) return false;
    if (c->alert_clear_s < 30 || c->alert_clear_s > 600) return false;
    // Hysteresis invariants (state-model.md §8).
    if (c->temp_alert_cc <= c->temp_clear_cc) return false;
    if (c->humid_alert_permil <= c->humid_clear_permil) return false;
    if (c->co2_alert_ppm <= c->co2_clear_ppm) return false;
    return true;
}

esp_err_t config_set(const ps_config_t *new_cfg)
{
    if (new_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Do NOT touch s_cfg here — config_task is the single writer. Enqueue a
    // full copy of the candidate config; config_task validates, applies, and
    // persists. If the queue is full, s_cfg remains unchanged (no partial
    // commit, unlike the previous in-place mutation design).
    config_event_t ev = {
        .type    = CONFIG_EVENT_APPLY_NEW,
        .new_cfg = *new_cfg,
    };
    if (xQueueSend(g_config_event_queue, &ev, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGE(TAG, "config_event_queue full; APPLY_NEW dropped, s_cfg unchanged");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// Persist a candidate config to NVS. Does NOT touch s_cfg — the caller is
// responsible for publishing to s_cfg only after this returns ESP_OK, so a
// persist failure cannot create memory/flash inconsistency (the runtime
// config remains at the previous value and a reboot reloads the previous
// value from flash). Writes go to the DEDICATED ps_cfg partition.
//
// Bounded retry: transient NVS errors (no free pages, not enough space) are
// retried up to 3 times with a 10 ms backoff. Permanent errors (partition
// not found, handle invalid, etc.) are NOT retried — they indicate a wiring
// or partition-table problem that won't resolve on its own. Each retry
// re-opens the handle (NVS handles are single-use after a failed commit).
#define NVS_RETRY_MAX        3U
#define NVS_RETRY_BACKOFF_MS 10U
static esp_err_t persist_candidate_to_nvs(const ps_config_t *candidate)
{
    esp_err_t ret = ESP_FAIL;
    for (uint32_t attempt = 0; attempt < NVS_RETRY_MAX; attempt++) {
        nvs_handle_t h;
        ret = nvs_open_from_partition(CONFIG_NVS_PARTITION,
                                      CONFIG_NVS_NAMESPACE,
                                      NVS_READWRITE, &h);
        if (ret != ESP_OK) {
            // Open failures are typically permanent (partition/handle issues).
            // No retry — log and bail.
            ESP_LOGE(TAG, "nvs_open_from_partition(RW): %s",
                     esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_set_blob(h, "cfg", candidate, sizeof(*candidate));
        if (ret == ESP_OK) {
            ret = nvs_commit(h);
        }
        nvs_close(h);

        if (ret == ESP_OK) {
            if (attempt > 0) {
                ESP_LOGI(TAG, "config persisted to NVS after %u retry(es)",
                         (unsigned)attempt);
            } else {
                ESP_LOGI(TAG, "config persisted to NVS (ps_cfg partition)");
            }
            return ESP_OK;
        }

        // Retry only on transient errors that may resolve after a GC cycle
        // or page consolidation. Permanent errors bail immediately.
        if (ret != ESP_ERR_NVS_NO_FREE_PAGES &&
            ret != ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
            ESP_LOGE(TAG, "nvs persist failed (permanent): %s",
                     esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGW(TAG, "nvs persist transient failure (attempt %u/%u): %s — "
                 "retrying in %u ms",
                 (unsigned)(attempt + 1), (unsigned)NVS_RETRY_MAX,
                 esp_err_to_name(ret), NVS_RETRY_BACKOFF_MS);
        vTaskDelay(pdMS_TO_TICKS(NVS_RETRY_BACKOFF_MS));
    }
    ESP_LOGE(TAG, "nvs persist failed after %u retries: %s",
             (unsigned)NVS_RETRY_MAX, esp_err_to_name(ret));
    return ret;
}

// Post an async result to g_config_result_queue. Never blocks config_task:
// if the queue is full, the OLDEST entry is drained (peek + receive) to make
// room for the newest. This means a slow consumer will miss stale results,
// which is acceptable — the newest result reflects the latest state.
static void post_config_result(config_result_kind_t kind, esp_err_t err)
{
    if (g_config_result_queue == NULL) {
        return;
    }
    config_result_t result = { .kind = kind, .err = err };
    if (xQueueSend(g_config_result_queue, &result, 0) != pdTRUE) {
        // Queue full — drain the oldest entry and retry. This is a
        // non-blocking peek-then-receive: worst case two other tasks raced
        // us and we still fail, in which case log and drop the result.
        config_result_t stale;
        if (xQueueReceive(g_config_result_queue, &stale, 0) == pdTRUE) {
            ESP_LOGW(TAG, "result queue full; dropped stale result (kind=%d)",
                     (int)stale.kind);
            (void)xQueueSend(g_config_result_queue, &result, 0);
        } else {
            ESP_LOGE(TAG, "result queue full; could not post result kind=%d",
                     (int)kind);
        }
    }
}

// Persist a mutex-guarded snapshot of s_cfg to NVS. Holding the mutex only
// for the memcpy keeps the critical section short (≤ 5 ms per
// task-architecture.md §6.1) and avoids blocking other readers during the
// (potentially slow) NVS write. Used by CONFIG_EVENT_PERSIST_NOW after
// internal edits that already hold the mutex (e.g. schema migration).
static esp_err_t persist_to_nvs(void)
{
    ps_config_t snapshot;
    if (xSemaphoreTake(s_cfg_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "persist_to_nvs: config_mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    memcpy(&snapshot, &s_cfg, sizeof(snapshot));
    xSemaphoreGive(s_cfg_mutex);
    return persist_candidate_to_nvs(&snapshot);
}

void config_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    config_event_t ev;

    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    for (;;) {
        if (xQueueReceive(g_config_event_queue, &ev,
                          pdMS_TO_TICKS(CONFIG_TASK_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            switch (ev.type) {
            case CONFIG_EVENT_PERSIST_NOW:
                // Persist current s_cfg as-is (used after internal edits that
                // already hold the mutex, e.g. schema migration).
                persist_to_nvs();
                break;
            case CONFIG_EVENT_APPLY_NEW: {
                // Single-writer path: validate → persist candidate → publish
                // to s_cfg, all in config_task context.
                //
                // ORDERING RATIONALE: the candidate is persisted to NVS
                // BEFORE being applied to s_cfg. A persist failure leaves
                // s_cfg unchanged, so the runtime state never diverges from
                // flash — a reboot reloads the previous (still-valid) config.
                // The previous "apply then persist" order could leave s_cfg
                // updated in RAM while the NVS write silently failed, causing
                // a reboot to revert to the old value with no warning.
                //
                // Validation rejects: wrong config_version, out-of-range
                // fields, or alert/clear hysteresis violations.
                //
                // RESULT POSTING: every APPLY_NEW outcome is acked via
                // g_config_result_queue so callers (e.g. state_machine_task
                // after a Matter ChangeToMode that updates config) can confirm
                // durability or surface an error to the controller.
                if (!config_validate(&ev.new_cfg)) {
                    ESP_LOGW(TAG, "APPLY_NEW rejected: out-of-range fields, "
                             "hysteresis violation, or version mismatch");
                    post_config_result(CONFIG_RESULT_REJECTED, ESP_OK);
                    break;
                }
                esp_err_t perr = persist_candidate_to_nvs(&ev.new_cfg);
                if (perr != ESP_OK) {
                    ESP_LOGE(TAG, "APPLY_NEW: persist failed (%s); s_cfg "
                             "unchanged — caller should retry",
                             esp_err_to_name(perr));
                    post_config_result(CONFIG_RESULT_PERSIST_FAILED, perr);
                    break;
                }
                // Persist succeeded — publish candidate to s_cfg under the
                // mutex. If the mutex take fails here, flash has the new
                // config but s_cfg still holds the old. That is a recoverable
                // inconsistency: a reboot will load the new config from NVS.
                // Log loudly and continue.
                if (xSemaphoreTake(s_cfg_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
                    ESP_LOGE(TAG, "APPLY_NEW: config_mutex timeout; flash "
                             "updated but s_cfg stale until next reboot");
                    post_config_result(CONFIG_RESULT_MUTEX_TIMEOUT, ESP_OK);
                    break;
                }
                s_cfg = ev.new_cfg;
                xSemaphoreGive(s_cfg_mutex);
                ESP_LOGI(TAG, "APPLY_NEW: persisted + s_cfg updated");
                post_config_result(CONFIG_RESULT_OK, ESP_OK);
                break;
            }
            case CONFIG_EVENT_UPDATE_PARAM:
                // TODO: per-key update + validate (future API)
                ESP_LOGW(TAG, "UPDATE_PARAM not yet implemented");
                break;
            case CONFIG_EVENT_FACTORY_RESET:
                // TODO: erase config namespace + signal state_machine_task
                //       + erase Matter factory dataset.
                ESP_LOGW(TAG, "FACTORY_RESET requested (not yet implemented)");
                break;
            default:
                ESP_LOGW(TAG, "unknown config event type %d", ev.type);
                break;
            }
        }
        // Feed TWDT every iteration. Max gap = CONFIG_TASK_QUEUE_TIMEOUT_MS
        // (2 s) + bounded NVS write time, well under the 10 s TWDT timeout
        // (task-architecture.md §7.1, §7.2).
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}
