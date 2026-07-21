// PrivacySense Matter Room Hub - room_state.c
//
// Minimal implementation of the shared room state with a FreeRTOS mutex.
// Only state_machine_task is allowed to call room_state_update(); readers
// use room_state_snapshot().

#include "room_state.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "room_state";

#define ROOM_STATE_MUTEX_TIMEOUT_MS   10U     // Hold ≤ 10 ms (task-architecture.md §6.1)
#define ROOM_STATE_SNAPSHOT_TIMEOUT_MS 50U

static SemaphoreHandle_t s_mutex = NULL;
static room_state_t      s_state;   // Single writer: state_machine_task

esp_err_t room_state_init(void)
{
    if (s_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex (heap exhausted?)");
        return ESP_ERR_NO_MEM;
    }

    // Default state: vacant, normal, OK, all sensors offline until proven online.
    memset(&s_state, 0, sizeof(s_state));
    s_state.occupancy           = OCCUPANCY_VACANT;
    s_state.user_mode           = MODE_NORMAL;
    s_state.pre_night_mode      = MODE_NORMAL;
    s_state.env_alert           = ALERT_OK;
    s_state.quiet_active        = false;
    s_state.wifi_connected         = false;
    s_state.matter_commissioned    = false;
    s_state.commissioning_active   = false;
    s_state.radar_online           = false;
    s_state.env_sensor_online      = false;

    ESP_LOGI(TAG, "init ok (sizeof(room_state_t)=%u)", (unsigned)sizeof(s_state));
    return ESP_OK;
}

// Exposed for state_machine_task diagnostics; do NOT use to update fields.
SemaphoreHandle_t g_room_state_mutex = NULL;

esp_err_t room_state_snapshot(room_state_t *out)
{
    if (out == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(ROOM_STATE_SNAPSHOT_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(out, &s_state, sizeof(*out));
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t room_state_update(const room_state_t *src)
{
    if (src == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(ROOM_STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "update deferred: mutex busy > %u ms", ROOM_STATE_MUTEX_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }
    memcpy(&s_state, src, sizeof(s_state));
    g_room_state_mutex = s_mutex;   // published once after init
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
