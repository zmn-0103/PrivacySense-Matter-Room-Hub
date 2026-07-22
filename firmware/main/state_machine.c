#include "state_machine.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "config.h"
#include "env_alert_sm.h"
#include "network.h"
#include "matter_app.h"
#include "mode_transition.h"
#include "night_window_sm.h"
#include "occ_sm.h"
#include "room_state.h"
#include "ui.h"

static const char *TAG = "state_machine";

#define APP_EVENT_QUEUE_DEPTH       32
#define MATTER_REPORT_QUEUE_DEPTH   8
#define STATE_MACHINE_QUEUE_TIMEOUT_MS 1000U
#define CONFIG_REREAD_INTERVAL_S    60U

static uint32_t s_radar_timeout_ms = 10000;

static uint32_t s_radar_watch_start_ms = 0;
static uint32_t s_last_valid_radar_ms = 0;
static bool    s_radar_timeout_latched = false;

QueueHandle_t g_app_event_queue       = NULL;
QueueHandle_t g_matter_report_queue   = NULL;

static occ_sm_t s_occ_sm;

static uint32_t s_entry_confirm_ms = 2000;
static uint32_t s_exit_delay_ms = 120000;

// Phase 2: env alert + NIGHT window state machines (pure logic modules).
// Both are platform-independent; state_machine_task feeds them with real
// sensor / time inputs and mirrors their results into room_state_t.
static env_alert_sm_t     s_env_alert_sm;
static env_alert_config_t s_env_alert_cfg;
static night_window_config_t s_night_cfg;

static bool s_matter_sync_pending = false;

static esp_err_t push_matter_report(matter_report_type_t type,
                                    occupancy_state_t occ,
                                    user_mode_t mode)
{
    matter_report_t rpt = { .type = type, .occupancy = occ, .user_mode = mode };
    if (xQueueSend(g_matter_report_queue, &rpt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "matter report dropped (type=%d)", (int)type);
        s_matter_sync_pending = true;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void config_fill_defaults(ps_config_t *cfg)
{
    cfg->entry_confirm_ms   = 2000;
    cfg->exit_delay_ms      = 120000;
    cfg->sensor_timeout_ms  = 10000;
    cfg->radar_eval_ms      = 200;
    cfg->night_start_min    = 1320;
    cfg->night_end_min      = 420;
    cfg->temp_alert_cc      = 3200;
    cfg->temp_clear_cc      = 3000;
    cfg->humid_alert_permil = 750;
    cfg->humid_clear_permil = 700;
    cfg->co2_alert_ppm      = 1000;
    cfg->co2_clear_ppm      = 800;
    cfg->alert_confirm_s    = 60;
    cfg->alert_clear_s      = 120;
    cfg->config_version     = CONFIG_VERSION_CURRENT;
}

// Seed the Phase 2 pure-logic configs with the same defaults as
// config_fill_defaults() so a failed config_get() at task start still
// leaves the SMs with sane thresholds. re_read_config() overwrites these
// from NVS when config_get() succeeds.
static void sm_configs_fill_defaults(void)
{
    s_env_alert_cfg.temp_alert_cc      = 3200;
    s_env_alert_cfg.temp_clear_cc      = 3000;
    s_env_alert_cfg.humid_alert_permil = 750;
    s_env_alert_cfg.humid_clear_permil = 700;
    s_env_alert_cfg.alert_confirm_ms   = 60000;
    s_env_alert_cfg.alert_clear_ms     = 120000;

    s_night_cfg.night_start_min = 1320;
    s_night_cfg.night_end_min   = 420;
}

esp_err_t state_machine_init(void)
{
    if (g_app_event_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    g_app_event_queue = xQueueCreate(APP_EVENT_QUEUE_DEPTH, sizeof(app_event_t));
    g_matter_report_queue = xQueueCreate(MATTER_REPORT_QUEUE_DEPTH, sizeof(matter_report_t));

    if (!g_app_event_queue || !g_matter_report_queue) {
        ESP_LOGE(TAG, "queue creation failed (heap exhausted?)");
        return ESP_ERR_NO_MEM;
    }

    occ_sm_init(&s_occ_sm, OCC_VACANT);
    env_alert_sm_init(&s_env_alert_sm, ENV_ALERT_OK);
    sm_configs_fill_defaults();

    ESP_LOGI(TAG, "init ok (app_event depth=%d, matter_report depth=%d)",
             APP_EVENT_QUEUE_DEPTH, MATTER_REPORT_QUEUE_DEPTH);
    return ESP_OK;
}

// Phase 2 time provider: returns valid wall-clock time after the first
// successful SNTP sync. Before SNTP sync, time_valid=false and
// night_window_sm_eval will not auto-enter NIGHT (graceful fallback).
//
// The SNTP client is initialised in network.c::init_sntp() and sets a
// power-cycle-scoped latch (network_time_is_synced()) on its first sync.
// Once synced, the latch survives Wi-Fi disconnects/reconnects so time
// stays valid throughout the uptime even if the NTP server is temporarily
// unreachable. The clock persists in ESP32 RTC across warm reboots but is
// lost on power cycle.
//
// Timezone is set in init_sntp() via setenv("TZ", ...) + tzset(). The
// pure NIGHT logic is fully host-tested via injected time inputs.
static night_window_time_t get_current_time(void)
{
    night_window_time_t t = { .time_valid = false, .local_minute = 0 };
    if (!network_time_is_synced()) {
        return t;
    }
    time_t now = time(NULL);
    struct tm result;
    struct tm *lt = localtime_r(&now, &result);
    if (lt != NULL) {
        t.local_minute = (uint16_t)(lt->tm_hour * 60 + lt->tm_min);
        t.time_valid = true;
    }
    return t;
}

static void process_radar(const ld2410c_radar_data_t *data)
{
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // ── valid frame ──
    if (data->valid) {
        s_last_valid_radar_ms = now_ms;
        s_radar_timeout_latched = false;

        room_state_t st;
        if (room_state_snapshot(&st) != ESP_OK) {
            ESP_LOGE(TAG, "process_radar: snapshot failed (valid frame)");
            return;
        }

        if (!st.radar_online) {
            st.radar_online = true;
            if (room_state_update(&st) == ESP_OK) {
                ESP_LOGI(TAG, "radar: offline -> online");
            }
        }

        occ_sm_t candidate = s_occ_sm;
        occ_state_t prev = candidate.state;
        if (occ_sm_eval(&candidate, data->target_present, now_ms,
                        s_entry_confirm_ms, s_exit_delay_ms)) {
            occupancy_state_t new_occ = (candidate.state == OCC_OCCUPIED)
                                         ? OCCUPANCY_OCCUPIED
                                         : OCCUPANCY_VACANT;

            room_state_t snap;
            if (room_state_snapshot(&snap) == ESP_OK) {
                snap.occupancy = new_occ;
                if (new_occ == OCCUPANCY_VACANT) {
                    snap.env_alert = ALERT_OK;
                }
                if (room_state_update(&snap) == ESP_OK) {
                    s_occ_sm = candidate;
                    // Phase 2: VACANT immediately clears env alert (spec §4.5).
                    // Sync the pure-logic SM here so it doesn't carry a stale
                    // ACTIVE state into the next OCCUPIED period (which would
                    // bypass the confirm cycle on VACANT → OCCUPIED re-entry).
                    // Only reset on actual VACANT transition; non-VACANT
                    // transitions leave the env alert SM to process_env.
                    if (new_occ == OCCUPANCY_VACANT) {
                        env_alert_sm_init(&s_env_alert_sm, ENV_ALERT_OK);
                    }
                    ESP_LOGI(TAG, "occupancy: %s -> %s",
                             prev == OCC_VACANT ? "VACANT"
                             : prev == OCC_OCCUPIED ? "OCCUPIED" : "UNKNOWN",
                             new_occ == OCCUPANCY_OCCUPIED ? "OCCUPIED" : "VACANT");
                    push_matter_report(MATTER_REPORT_OCCUPANCY,
                                       new_occ, snap.user_mode);
                }
            }
        } else {
            s_occ_sm = candidate;
        }
        return;
    }

    // ── invalid frame: sensor timeout ──
    // Latch moved to end: all room_state ops must succeed before latching,
    // so a failed snapshot/update retries on the next frame.
    if (s_radar_timeout_latched) return;

    uint32_t elapsed_ms = (s_last_valid_radar_ms == 0)
                           ? (now_ms - s_radar_watch_start_ms)
                           : (now_ms - s_last_valid_radar_ms);
    if (elapsed_ms < s_radar_timeout_ms) return;

    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) return;
    if (st.radar_online) {
        st.radar_online = false;
        if (room_state_update(&st) != ESP_OK) {
            ESP_LOGE(TAG, "process_radar: update failed (online -> offline)");
            return;
        }
        ESP_LOGI(TAG, "radar: online -> offline "
                 "(elapsed=%u ms, thr=%u ms, reason=%d)",
                 (unsigned)elapsed_ms, (unsigned)s_radar_timeout_ms,
                 (int)data->failure);
    }

    if (s_occ_sm.state != OCC_UNKNOWN) {
        occ_state_t prev = s_occ_sm.state;
        room_state_t snap;
        if (room_state_snapshot(&snap) != ESP_OK) return;
        snap.occupancy = OCCUPANCY_UNKNOWN;
        if (room_state_update(&snap) != ESP_OK) return;
        s_occ_sm.state = OCC_UNKNOWN;
        s_occ_sm.timer_start_ms = 0;
        s_occ_sm.timer_active = false;
        // Freeze env alert SM: preserve state, reset timers so a full
        // confirm/clear cycle is required when occupancy resumes. Without
        // this, partially accumulated timer values can cross the UNKNOWN
        // period and trigger premature alert transitions after recovery.
        s_env_alert_sm.timer_active   = false;
        s_env_alert_sm.timer_start_ms = 0;
        s_env_alert_sm.counting_alert = false;
        ESP_LOGI(TAG, "occupancy: %s -> UNKNOWN (sensor timeout)",
                 prev == OCC_VACANT ? "VACANT" : "OCCUPIED");
    }

    s_radar_timeout_latched = true;
}

// Refresh runtime config from NVS and resync all Phase 1 + Phase 2 SM
// configs. Called from state_machine_task on EVENT_CONFIG_CHANGE and on the
// 60 s periodic reread. Phase 2 additions:
//   - s_env_alert_cfg rebuilt from ps_config_t thresholds + confirm/clear
//   - s_night_cfg rebuilt from night_start/end_min
//   - env_alert_sm confirm/clear timer RESET on config change so a fresh
//     cycle is required under the new thresholds (state-model.md §4.4).
//     State (OK/ACTIVE) is preserved.
static esp_err_t re_read_config(ps_config_t *cfg)
{
    esp_err_t err = config_get(cfg);
    if (err == ESP_OK) {
        // Snapshot old env alert config so we can detect actual changes.
        env_alert_config_t old_cfg = s_env_alert_cfg;

        s_radar_timeout_ms  = cfg->sensor_timeout_ms;
        s_entry_confirm_ms  = cfg->entry_confirm_ms;
        s_exit_delay_ms     = cfg->exit_delay_ms;

        s_env_alert_cfg.temp_alert_cc      = cfg->temp_alert_cc;
        s_env_alert_cfg.temp_clear_cc      = cfg->temp_clear_cc;
        s_env_alert_cfg.humid_alert_permil = cfg->humid_alert_permil;
        s_env_alert_cfg.humid_clear_permil = cfg->humid_clear_permil;
        s_env_alert_cfg.alert_confirm_ms   = (uint32_t)cfg->alert_confirm_s * 1000U;
        s_env_alert_cfg.alert_clear_ms     = (uint32_t)cfg->alert_clear_s * 1000U;

        s_night_cfg.night_start_min = cfg->night_start_min;
        s_night_cfg.night_end_min   = cfg->night_end_min;

        // Only reset confirm/clear timer when env alert thresholds or timeout
        // values actually changed. NIGHT window changes are independent of env
        // alert and MUST NOT reset the env alert timer (three state dimensions
        // are orthogonal). The periodic 60 s reread (which fires even when
        // config is unchanged) must NOT wipe in-progress timers either — doing
        // so would prevent the default 60 s confirm / 120 s clear from ever
        // completing.
        if (old_cfg.temp_alert_cc      != s_env_alert_cfg.temp_alert_cc      ||
            old_cfg.temp_clear_cc      != s_env_alert_cfg.temp_clear_cc      ||
            old_cfg.humid_alert_permil != s_env_alert_cfg.humid_alert_permil ||
            old_cfg.humid_clear_permil != s_env_alert_cfg.humid_clear_permil ||
            old_cfg.alert_confirm_ms   != s_env_alert_cfg.alert_confirm_ms   ||
            old_cfg.alert_clear_ms     != s_env_alert_cfg.alert_clear_ms) {
            s_env_alert_sm.timer_active   = false;
            s_env_alert_sm.timer_start_ms = 0;
            s_env_alert_sm.counting_alert = false;
        }
    }
    return err;
}

static void radar_check_timeout(void)
{
    if (s_radar_timeout_latched) return;

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t elapsed_ms = (s_last_valid_radar_ms == 0)
                           ? (now_ms - s_radar_watch_start_ms)
                           : (now_ms - s_last_valid_radar_ms);
    if (elapsed_ms < s_radar_timeout_ms) return;

    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) return;
    if (st.radar_online) {
        st.radar_online = false;
        if (room_state_update(&st) != ESP_OK) {
            ESP_LOGE(TAG, "radar_check_timeout: update failed (online -> offline)");
            return;
        }
        ESP_LOGI(TAG, "radar: online -> offline "
                 "(periodic check, elapsed=%u ms, thr=%u ms)",
                 (unsigned)elapsed_ms, (unsigned)s_radar_timeout_ms);
    }

    if (s_occ_sm.state != OCC_UNKNOWN) {
        occ_state_t prev = s_occ_sm.state;
        room_state_t snap;
        if (room_state_snapshot(&snap) != ESP_OK) return;
        snap.occupancy = OCCUPANCY_UNKNOWN;
        if (room_state_update(&snap) != ESP_OK) return;
        s_occ_sm.state = OCC_UNKNOWN;
        s_occ_sm.timer_start_ms = 0;
        s_occ_sm.timer_active = false;
        // Freeze env alert SM (same rationale as process_radar).
        s_env_alert_sm.timer_active   = false;
        s_env_alert_sm.timer_start_ms = 0;
        s_env_alert_sm.counting_alert = false;
        ESP_LOGI(TAG, "occupancy: %s -> UNKNOWN (sensor timeout, periodic)",
                 prev == OCC_VACANT ? "VACANT" : "OCCUPIED");
    }

    s_radar_timeout_latched = true;
}

// Phase 2: feed occupancy + sensor sample into the env alert pure logic and
// mirror any state change into room_state_t.env_alert. Uses the same
// candidate pattern as process_radar: SM is committed only after room_state
// reflects the transition; on failure the candidate is discarded and the
// next eval re-evaluates from the old SM state.
//
// Phase 2 does NOT push a Matter report for env alert — state-model.md §4
// keeps env alert local-only (RGB/OLED); no env cluster is exposed.
static void evaluate_env_alert(const room_state_t *snap,
                               const env_alert_input_t *input,
                               uint32_t now_ms)
{
    bool is_vacant  = (snap->occupancy == OCCUPANCY_VACANT);
    bool is_unknown = (snap->occupancy == OCCUPANCY_UNKNOWN);

    env_alert_sm_t candidate = s_env_alert_sm;
    bool changed = env_alert_sm_eval(&candidate, input,
                                     is_vacant, is_unknown,
                                     now_ms, &s_env_alert_cfg);

    if (!changed) {
        // Commit non-transition updates (timer progress / freeze reset).
        s_env_alert_sm = candidate;
        return;
    }

    // State changed: update room_state first, then commit SM.
    env_alert_t new_alert = (candidate.state == ENV_ALERT_ACTIVE)
                              ? ALERT_ACTIVE : ALERT_OK;
    room_state_t st = *snap;
    st.env_alert = new_alert;
    if (room_state_update(&st) != ESP_OK) {
        ESP_LOGE(TAG, "env alert: room_state update failed (SM not committed)");
        return;
    }
    s_env_alert_sm = candidate;

    ESP_LOGI(TAG, "env_alert: %s -> %s",
             snap->env_alert == ALERT_OK ? "OK" : "ALERT",
             new_alert == ALERT_OK ? "OK" : "ALERT");
}

static void process_env(const env_sensor_data_t *data)
{
    static uint32_t s_proto_fail = 0;
    static uint32_t s_range_fail = 0;

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (data->valid) {
        s_proto_fail = 0;
        s_range_fail = 0;
        room_state_t st;
        if (room_state_snapshot(&st) != ESP_OK) {
            ESP_LOGE(TAG, "process_env: snapshot failed (valid sample)");
            return;
        }
        if (!st.env_sensor_online) {
            st.env_sensor_online = true;
            if (room_state_update(&st) != ESP_OK) {
                ESP_LOGE(TAG, "process_env: update failed (offline -> online)");
                // Continue to env alert eval with the stale snapshot; the
                // online flag will retry on the next sample.
            } else {
                ESP_LOGI(TAG, "env sensor: offline -> online");
            }
            // Re-snapshot so evaluate_env_alert sees the committed online
            // flag (matters for the freeze rule: sensor_valid is derived
            // from data->valid here, not from st.env_sensor_online, but we
            // pass sensor_valid=true below regardless).
        }

        // Phase 2: env alert evaluation (spec §4). sensor_valid=true because
        // data->valid is true. Occupancy comes from the (possibly just-updated)
        // room_state snapshot.
        env_alert_input_t input = {
            .temp_cc       = data->temperature_cc,
            .humid_permil  = data->humidity_permil,
            .sensor_valid  = true,
        };
        evaluate_env_alert(&st, &input, now_ms);
        return;
    }

    bool is_range = (data->failure == ENV_SENSOR_FAIL_RANGE);
    if (is_range) {
        s_range_fail++;
        s_proto_fail = 0;
    } else {
        s_proto_fail++;
        s_range_fail = 0;
    }
    uint32_t count = is_range ? s_range_fail : s_proto_fail;
    uint32_t threshold = is_range ? 5 : 3;

    if (count < threshold) return;

    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) {
        ESP_LOGE(TAG, "process_env: snapshot failed (threshold reached)");
        return;
    }
    if (st.env_sensor_online) {
        st.env_sensor_online = false;
        if (room_state_update(&st) != ESP_OK) {
            ESP_LOGE(TAG, "process_env: update failed (online -> offline)");
        } else {
            ESP_LOGI(TAG, "env sensor: online -> offline "
                     "(count=%u, thr=%u, reason=%d)",
                     (unsigned)count, (unsigned)threshold,
                     (int)data->failure);
        }
    }

    // Phase 2: sensor just went offline (or was already). Freeze env alert
    // SM — this resets the confirm/clear timer so a fresh cycle is required
    // when the sensor recovers (prevents stale-timer exploits).
    env_alert_input_t offline_input = { .temp_cc = 0, .humid_permil = 0,
                                        .sensor_valid = false };
    evaluate_env_alert(&st, &offline_input, now_ms);
}

static void process_button(button_event_t event)
{
    if (event == BUTTON_EVENT_LONG_PRESS) {
        // Phase 3 Step 4: factory reset on confirmed long-press (≥ 5 s).
        // Commissioning-lifecycle.md §5: erase business config (ps_cfg
        // partition) first, then erase default NVS (Wi-Fi/Matter/BLE
        // credentials), green LED confirmation, reboot.
        //
        // Anti-mistouch is already enforced by button_task: the LONG_PRESS
        // event is only emitted after the user holds the button for ≥ 5 s
        // AND releases it. The 5 s red countdown UI provides real-time
        // feedback. Release before 5 s cancels the operation.
        ESP_LOGW(TAG, "LONG_PRESS: factory reset confirmed by user");
        ESP_LOGW(TAG, "=== FACTORY RESET SEQUENCE START ===");

        // Step 1: erase business config (ps_cfg partition → defaults).
        esp_err_t ret = config_factory_reset();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FACTORY RESET FAILED at step 1 (config_factory_reset: %s) — "
                     "ps_cfg partition NOT erased; aborting reset",
                     esp_err_to_name(ret));
            ui_show_factory_reset_failed();
            return;
        }

        // Step 2: green LED confirmation, then system-level factory reset.
        // matter_app_factory_reset() erases the default NVS partition
        // (Wi-Fi/Matter/BLE credentials), restores Wi-Fi config, and
        // reboots. It does NOT return.
        ui_show_factory_reset_confirm();
        ESP_LOGI(TAG, "business config erased; proceeding to system reset...");
        matter_app_factory_reset();
        // Unreachable — matter_app_factory_reset calls esp_restart().
        return;
    }

    // SHORT_PRESS: NIGHT ↔ QUIET toggle within NIGHT, or NORMAL ↔ QUIET.
    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) {
        ESP_LOGE(TAG, "BUTTON: snapshot failed");
        return;
    }

    user_mode_t old_mode = st.user_mode;
    bool old_quiet = st.quiet_active;

    mode_transition_result_t r = mode_transition_short_press(st.user_mode,
                                                              st.quiet_active,
                                                              st.pre_night_mode);
    st.user_mode      = r.user_mode;
    st.quiet_active   = r.quiet_active;
    st.pre_night_mode = r.pre_night_mode;

    if (room_state_update(&st) != ESP_OK) {
        ESP_LOGE(TAG, "BUTTON: update failed");
        return;
    }

    ESP_LOGI(TAG, "BUTTON: mode %s→%s, quiet %d→%d",
             old_mode == MODE_NORMAL ? "NORMAL" :
             old_mode == MODE_QUIET  ? "QUIET"  : "NIGHT",
             r.user_mode == MODE_NORMAL ? "NORMAL" :
             r.user_mode == MODE_QUIET  ? "QUIET"  : "NIGHT",
             (int)old_quiet, (int)r.quiet_active);

    (void)push_matter_report(MATTER_REPORT_CURRENT_MODE,
                             st.occupancy, st.user_mode);
}

static void process_network(network_status_t status)
{
    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) {
        ESP_LOGE(TAG, "process_network: snapshot failed");
        return;
    }

    bool changed = false;

    switch (status) {
    case NETWORK_STATUS_CONNECTED:
        if (!st.wifi_connected) {
            st.wifi_connected = true;
            changed = true;
            ESP_LOGI(TAG, "network: disconnected -> connected");
            // Wi-Fi reconnected — schedule a force-sync of all attributes
            // once the Matter session is available (matter-data-model.md §6.3).
            push_matter_report(MATTER_REPORT_FORCE_SYNC,
                               st.occupancy, st.user_mode);
        }
        break;

    case NETWORK_STATUS_DISCONNECTED:
        if (st.wifi_connected) {
            st.wifi_connected = false;
            changed = true;
            ESP_LOGI(TAG, "network: connected -> disconnected");
        }
        break;

    case NETWORK_STATUS_PROVISIONED:
        // Credentials received during commissioning. The actual link-up
        // is tracked by the subsequent CONNECTED / DISCONNECTED events.
        // wifi_connected is NOT set here — wait for GOT_IP.
        ESP_LOGI(TAG, "network: provisioned (credentials received, awaiting link)");
        break;
    }

    if (changed) {
        if (room_state_update(&st) != ESP_OK) {
            ESP_LOGE(TAG, "process_network: update failed");
        }
    }
}

// Phase 3 Step 3/5: Matter commissioning lifecycle handler.
// Receives EVENT_MATTER_LIFECYCLE from matter_app.cpp callbacks and updates
// matter_commissioned / commissioning_active in room_state.
//
// Commissioning lifecycle (commissioning-lifecycle.md §3.2):
//   WindowOpened → commissioning_active = true
//   SessionStarted → (PAKE exchange)
//     ┌─ CommissioningComplete → matter_commissioned = true, commissioning_active = false
//     └─ SessionStopped (no fabric) → no change
//   WindowClosed (5 min timeout) → commissioning_active = false
//   FabricRemoved → check remaining_fabrics; only clear matter_commissioned if 0
static void process_matter_lifecycle(const matter_lifecycle_t *lifecycle)
{
    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) {
        ESP_LOGE(TAG, "process_matter_lifecycle: snapshot failed");
        return;
    }

    switch (lifecycle->event) {
    case MATTER_LIFECYCLE_COMMISSIONING_COMPLETE:
        st.matter_commissioned = true;
        st.commissioning_active = false;
        ESP_LOGI(TAG, "matter: commissioning complete — fabric established");
        break;

    case MATTER_LIFECYCLE_WINDOW_OPENED:
        st.commissioning_active = true;
        ESP_LOGI(TAG, "matter: commissioning window opened (BLE advertising)");
        break;

    case MATTER_LIFECYCLE_WINDOW_CLOSED:
        st.commissioning_active = false;
        ESP_LOGI(TAG, "matter: commissioning window closed (5 min timeout)");
        break;

    case MATTER_LIFECYCLE_FABRIC_REMOVED:
        // Phase 3 Step 5: only clear matter_commissioned when ALL fabrics
        // are gone. In multi-admin scenarios with multiple fabrics, losing
        // one fabric does not decommission the device.
        if (lifecycle->remaining_fabrics == 0) {
            st.matter_commissioned = false;
            ESP_LOGI(TAG, "matter: last fabric removed — matter_commissioned cleared "
                     "(re-commissioning needed)");
        } else {
            ESP_LOGI(TAG, "matter: fabric removed, %u fabric(s) remain — "
                     "matter_commissioned unchanged",
                     (unsigned)lifecycle->remaining_fabrics);
            return;  // no room_state change needed
        }
        break;

    case MATTER_LIFECYCLE_SESSION_STOPPED:
        // PASE session ended without completing. commissioning_active stays
        // true — the window may still be open for another attempt.
        ESP_LOGI(TAG, "matter: commissioning session stopped (window may still be open)");
        return;  // no room_state change
    }

    if (room_state_update(&st) != ESP_OK) {
        ESP_LOGE(TAG, "process_matter_lifecycle: update failed");
    }
}

static void process_matter_command(const matter_command_t *cmd)
{
    (void)cmd;
}

// Phase 2: NIGHT window evaluation via the pure-logic night_window_sm.
// Syncs (user_mode, pre_night_mode, quiet_active) from room_state into a
// local night_window_sm_state_t, runs the pure eval, and writes back ONLY
// if user_mode changed. quiet_active is never modified here — short-press
// toggling within NIGHT is owned by mode_transition.c.
//
// Phase 2: time is obtained via get_current_time() which returns valid
// wall-clock minutes after the first SNTP sync. Before sync, time_valid
// is false and the function stays in the current mode (no phantom NIGHT
// entry at epoch). The pure NIGHT logic is fully host-tested via injected
// {time_valid, local_minute} inputs.
//
// Local state update does NOT depend on Matter queue success: even if
// push_matter_report drops the report, room_state is already updated and
// s_matter_sync_pending will trigger a force-sync retry on the next loop.
static void evaluate_night_window(uint32_t now_ms, const ps_config_t *cfg)
{
    (void)now_ms;

    room_state_t st;
    if (room_state_snapshot(&st) != ESP_OK) {
        ESP_LOGE(TAG, "evaluate_night_window: snapshot failed");
        return;
    }

    night_window_sm_state_t nws = {
        .user_mode      = (night_window_mode_t)st.user_mode,
        .pre_night_mode = (night_window_mode_t)st.pre_night_mode,
        .quiet_active   = st.quiet_active,
    };
    night_window_time_t t = get_current_time();
    night_window_config_t nwc = {
        .night_start_min = cfg->night_start_min,
        .night_end_min   = cfg->night_end_min,
    };

    if (!night_window_sm_eval(&nws, &t, &nwc)) return;

    // State changed: write back. quiet_active is unchanged by the pure
    // function; copy through to keep room_state consistent.
    user_mode_t prev_mode = st.user_mode;
    st.user_mode      = (user_mode_t)nws.user_mode;
    st.pre_night_mode = (user_mode_t)nws.pre_night_mode;
    st.quiet_active   = nws.quiet_active;

    if (room_state_update(&st) != ESP_OK) {
        ESP_LOGE(TAG, "evaluate_night_window: update failed");
        return;
    }

    ESP_LOGI(TAG, "night window: mode %s -> %s",
             prev_mode == MODE_NORMAL ? "NORMAL" :
             prev_mode == MODE_QUIET  ? "QUIET"  : "NIGHT",
             st.user_mode == MODE_NORMAL ? "NORMAL" :
             st.user_mode == MODE_QUIET  ? "QUIET"  : "NIGHT");

    (void)push_matter_report(MATTER_REPORT_CURRENT_MODE,
                             st.occupancy, st.user_mode);
}

void state_machine_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ps_config_t cfg;
    config_fill_defaults(&cfg);
    // Phase 2 fix: previously this block duplicated the s_* assignments that
    // already live in re_read_config(). Call re_read_config() instead so the
    // Phase 1 + Phase 2 SM configs (env alert thresholds, NIGHT window) are
    // refreshed through a single code path. On failure, s_* keep their
    // state_machine_init() / sm_configs_fill_defaults() values.
    if (re_read_config(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "config_get failed; using defaults"
                 " (sensor_timeout=%u entry_confirm=%u exit_delay=%u)",
                 (unsigned)s_radar_timeout_ms,
                 (unsigned)s_entry_confirm_ms,
                 (unsigned)s_exit_delay_ms);
    }

    s_radar_watch_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    uint32_t loop_count = 0;
    TickType_t last_config_reread_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "task started (stack %u bytes, prio %d)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (int)uxTaskPriorityGet(NULL));

    for (;;) {
        app_event_t ev;
        if (xQueueReceive(g_app_event_queue, &ev,
                          pdMS_TO_TICKS(STATE_MACHINE_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            switch (ev.type) {
            case EVENT_RADAR_DATA:
                process_radar(&ev.data.radar);
                break;
            case EVENT_ENV_DATA:
                process_env(&ev.data.env);
                break;
            case EVENT_BUTTON:
                process_button(ev.data.button);
                break;
            case EVENT_NETWORK_STATUS:
                process_network(ev.data.network);
                break;
            case EVENT_MATTER_COMMAND:
                process_matter_command(&ev.data.matter_cmd);
                break;
            case EVENT_MATTER_LIFECYCLE:
                process_matter_lifecycle(&ev.data.matter_lifecycle);
                break;
            case EVENT_MATTER_READ:
            case EVENT_TIMER_1S:
                break;
            case EVENT_CONFIG_CHANGE:
                if (re_read_config(&cfg) == ESP_OK) {
                    ESP_LOGI(TAG, "config re-read ok"
                             " (entry=%u exit=%u timeout=%u)",
                             (unsigned)s_entry_confirm_ms,
                             (unsigned)s_exit_delay_ms,
                             (unsigned)s_radar_timeout_ms);
                } else {
                    ESP_LOGW(TAG, "config re-read failed");
                }
                break;
            default:
                ESP_LOGW(TAG, "unknown event type %d", ev.type);
                break;
            }
        }

        evaluate_night_window(xTaskGetTickCount() * portTICK_PERIOD_MS, &cfg);
        radar_check_timeout();

        if (s_matter_sync_pending) {
            room_state_t rs;
            if (room_state_snapshot(&rs) == ESP_OK) {
                matter_report_t rpt = {
                    .type = MATTER_REPORT_FORCE_SYNC,
                    .occupancy = rs.occupancy,
                    .user_mode = rs.user_mode,
                };
                if (xQueueSend(g_matter_report_queue, &rpt, 0) == pdTRUE) {
                    s_matter_sync_pending = false;
                    ESP_LOGI(TAG, "matter sync retry ok");
                }
            }
        }

        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop_count % 60) == 0) {
            ESP_LOGI(TAG, "heartbeat: loop=%u, stack_hwm=%u bytes",
                     (unsigned)loop_count,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        TickType_t now_tick = xTaskGetTickCount();
        TickType_t interval = pdMS_TO_TICKS(CONFIG_REREAD_INTERVAL_S * 1000);
        if ((now_tick - last_config_reread_tick) >= interval) {
            last_config_reread_tick = now_tick;
            if (re_read_config(&cfg) == ESP_OK) {
                ESP_LOGI(TAG, "config re-read ok"
                         " (entry=%u exit=%u timeout=%u)",
                         (unsigned)s_entry_confirm_ms,
                         (unsigned)s_exit_delay_ms,
                         (unsigned)s_radar_timeout_ms);
            } else {
                ESP_LOGW(TAG, "config re-read failed, retrying in 30 s");
                last_config_reread_tick -= (interval - pdMS_TO_TICKS(30000));
            }
        }
    }
}
