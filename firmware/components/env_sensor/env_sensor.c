// PrivacySense Matter Room Hub - environment sensor implementation
//
// Skeleton. Real DHT22 40-bit protocol handling arrives in a follow-up commit
// after first build succeeds. This file currently:
//   - Configures GPIO2 (open-drain output + input) for the DHT22 start signal
//   - Configures an RMT RX channel at 1 MHz (1 µs tick) for pulse capture
//   - Spawns sensor_env_task (5 s period)
//   - On each period: drives the start signal, arms RX, waits ≤ 100 ms for
//     the done event, then emits a placeholder (valid=false) so the state
//     machine can exercise its env-alert / sensor-online path.
//
// Ownership (task-architecture.md §4.2, §5.1, §7.2):
//   - sensor_env_task is the ONLY writer to GPIO2 and the ONLY caller of the
//     registered data callback.
//   - TWDT feed gap ≤ 5 s (sample period). TWDT timeout is 10 s, so a single
//     missed sample still leaves headroom (task-architecture.md §7.1).

#include "env_sensor.h"
#include "env_sensor_parser.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_rx.h"

static const char *TAG = "env_sensor";

#define ENV_TASK_STACK         4096
#define ENV_TASK_PRIO          4
#define ENV_SAMPLE_PERIOD_MS   5000U    // state-model.md §4.2 (≥ 2000 ms)
#define ENV_RMT_TIMEOUT_MS     100U     // task-architecture.md §4.2 (≤ 100 ms total transaction)
#define ENV_FAIL_THRESHOLD     3U       // state-model.md §5.2 (3 consecutive fails → offline)

// DHT22 protocol timing (microseconds). Used by the real parser (TODO).
#define DHT22_START_LOW_US     18000U   // ≥ 18 ms host start low
#define DHT22_START_HIGH_US    40U      // host release, wait for sensor
#define DHT22_SENSOR_RESP_LOW  80U      // sensor response low
#define DHT22_SENSOR_RESP_HIGH 80U      // sensor response high
#define DHT22_BIT_ZERO_US      27U      // ~26-28 µs for bit 0
#define DHT22_BIT_ONE_US       70U      // ~70 µs for bit 1

// RMT RX memory block size, in rmt_symbol_word_t units. The DHT22 stream is
// 41 symbols minimum (1 response + 40 bit-pulses); round up to 64 for margin.
#define ENV_RMT_MEM_SYMBOLS   64U

static gpio_num_t                  s_data_gpio    = GPIO_NUM_NC;
static env_sensor_data_callback_t  s_callback     = NULL;
static TaskHandle_t                s_task_handle  = NULL;
static rmt_channel_handle_t        s_rmt_rx_chan  = NULL;
static SemaphoreHandle_t          s_rx_done_sem   = NULL;

// Transaction ID to defeat late callbacks. DESIGN:
//
//   s_armed_txn — written by the task BEFORE each rmt_receive() call, set to
//      0 when no arm is in progress (between iterations or after timeout).
//      Read by the ISR. The ISR IGNORES the callback if s_armed_txn == 0,
//      preventing a late callback from giving a stale semaphore token.
//
//   s_expected_txn — monotonically increasing counter, bumped by the task
//      each iteration. this_txn is a local snapshot taken after the bump.
//
//   s_last_txn — written by the ISR from s_armed_txn. Compared against
//      this_txn by the task after semaphore take.
//
// Why not s_expected_txn in the ISR? If the task times out, bumps
// s_expected_txn, and arms a new receive, a late ISR would read the NEW
// s_expected_txn and incorrectly stamp the stale callback as belonging to
// the new transaction. s_armed_txn avoids this: the task clears it to 0
// between transactions, so the stale ISR sees 0 and skips.
//
// Edge case: a stale ISR could fire after s_armed_txn is set for the new
// arm but before the new DMA completes. In that case s_armed_txn != 0 and
// the ISR gives a semaphore with stale symbol count. The subsequent
// parse_dht22() call has a degraded symbol count vs buffer content match
// and almost certainly fails the parse — at most one sample period with a
// failed parse (→ retry on next 5 s period).
static volatile uint8_t s_expected_txn = 0;
static volatile uint8_t s_armed_txn    = 0;   // txn ID at arm time (set by task, read by ISR)
static volatile uint8_t s_last_txn     = 0;

// RMT receive buffer for one DHT22 transaction (41 symbols minimum).
// ESP-IDF v5.4 new RMT RX API (rmt_receive) requires a caller-owned buffer.
static rmt_symbol_word_t s_rx_buf[ENV_RMT_MEM_SYMBOLS];

// Number of valid symbols captured in the last RX-done ISR. Written by ISR
// after the receive completes, read by the task after semaphore take.
// Safe because the ISR give happens-after the write, and the task take
// happens-before the read (full memory barrier via the semaphore).
//
// NOTE: this is only meaningful when s_last_txn == s_expected_txn at the
// read site. A late callback from a previous transaction may have written a
// stale symbol count that must NOT be consumed by the current iteration.
static volatile size_t s_last_num_symbols = 0;

// RMT receive config: pulse widths outside [min, max] are treated as gaps.
// DHT22 pulses: 27–80 µs. Use 3 µs floor (noise filter) and 100 µs ceiling.
//
// ESP32-C6 RMT filter hardware limit (ESP-IDF v5.4, rmt_ll.h):
//   RMT_LL_MAX_FILTER_VALUE = 255 (8-bit), filter clock = 80 MHz (PLL_F80M),
//   so max signal_range_min_ns = 255*1e9/80e6 = 3187 ns. 10 µs (10000 ns)
//   yields filter_reg_value=800 > 255 → rmt_receive() returns
//   ESP_ERR_INVALID_ARG and NO sample ever succeeds. 3 µs (3000 ns) → 240,
//   safely under the limit; DHT22 shortest pulse ≈ 27 µs so no valid data
//   is filtered.
// NOTE: rmt_receive_config_t in ESP-IDF v5.4 has NO flags.invert_in field;
// input inversion is configured in rmt_rx_channel_config_t.flags instead.
#define DHT22_SIGNAL_MIN_NS   (3UL   * 1000U)   // 3 µs (C6 max: 3187 ns)
#define DHT22_SIGNAL_MAX_NS   (100UL * 1000U)   // 100 µs
static const rmt_receive_config_t s_rx_recv_cfg = {
    .signal_range_min_ns = DHT22_SIGNAL_MIN_NS,
    .signal_range_max_ns = DHT22_SIGNAL_MAX_NS,
};

static bool IRAM_ATTR on_rmt_rx_done(rmt_channel_handle_t chan,
                                     const rmt_rx_done_event_data_t *edata,
                                     void *user_ctx)
{
    (void)chan;
    (void)user_ctx;
    // ISR context: read s_armed_txn (arm-time txn ID). If the task has
    // cleared it to 0 (between transactions or after timeout), this is a
    // stale callback — ignore it entirely (no sem give, no data write).
    // This avoids a late callback from a previous timed-out transaction
    // injecting stale symbols into the current iteration.
    uint8_t txn = s_armed_txn;
    if (txn == 0) {
        return false;
    }
    s_last_num_symbols = edata->num_symbols;
    s_last_txn         = txn;
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_rx_done_sem, &high_task_woken);
    return (high_task_woken == pdTRUE);
}

// Diagnostics: one-shot symbol dump on first failure.
static bool s_diag_printed        = false;
// Rate-limit: log threshold only once per offline episode.
static bool s_threshold_logged    = false;

// Convert rmt_symbol_word_t (ESP-IDF RMT bitfield) → dht22_symbol_t (parser).
// The ESP-IDF bitfield packs duration+level into each uint16_t; the plain
// struct uses separate fields. Conversion is needed because env_sensor_parser.h
// is self-contained (no ESP-IDF deps) for host-side testing.
static dht22_symbol_t rmt_to_dht22_sym(const rmt_symbol_word_t *src)
{
    dht22_symbol_t dst;
    dst.duration0 = src->duration0;
    dst.duration1 = src->duration1;
    dst.level0    = src->level0;
    dst.level1    = src->level1;
    return dst;
}

// Parse 40-bit DHT22 frame: convert RMT symbols → dht22_symbol_t, then
// delegate to the shared dht22_parse_symbols(). Maps the result back to
// env_sensor_data_t / env_sensor_failure_t.
static env_sensor_failure_t parse_rmt_dht22(const rmt_symbol_word_t *items,
                                            size_t num,
                                            env_sensor_data_t *out)
{
    if (num < 41) return ENV_SENSOR_FAIL_PROTOCOL;

    dht22_symbol_t converted[64];
    size_t convert_n = (num > 64) ? 64 : num;
    for (size_t i = 0; i < convert_n; i++) {
        converted[i] = rmt_to_dht22_sym(&items[i]);
    }

    dht22_sample_t parsed = {0};
    dht22_status_t st = dht22_parse_symbols(converted, convert_n, &parsed);
    if (st == DHT22_OK) {
        out->temperature_cc  = parsed.temperature_cc;
        out->humidity_permil = parsed.humidity_permil;
        out->co2_ppm         = 0;
        out->valid           = true;
        out->failure         = ENV_SENSOR_OK;
        return ENV_SENSOR_OK;
    }
    // Map dht22_status_t → env_sensor_failure_t
    out->failure = (st == DHT22_FAIL_RANGE) ? ENV_SENSOR_FAIL_RANGE
                  : ENV_SENSOR_FAIL_PROTOCOL;
    return out->failure;
}

// Pull DATA low ≥ 18 ms (DHT22 host start signal). The bus is left LOW on
// return; the caller MUST (1) arm RMT RX, then (2) release the bus via
// gpio_set_level(gpio, 1). Arming RX before release ensures the RMT engine
// is ready to capture the sensor's response edge that arrives ~30 µs after
// release — task preemption between arm and release cannot lose samples.
//
// GPIO_MODE_INPUT_OUTPUT_OD is used (not OUTPUT_OD): the input path stays
// enabled so the RMT RX channel can sample the line through the same GPIO
// while we drive it. OUTPUT_OD would disable the input buffer and the RMT
// engine would never see the sensor's response.
static void drive_start_signal(gpio_num_t gpio)
{
    gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));        // ≥ 18 ms per DHT22 datasheet
    // Bus is held LOW here; caller will release after arming RMT.
}

static void emit_failure(uint32_t timestamp_ms, env_sensor_failure_t reason)
{
    env_sensor_data_t sample = {
        .timestamp_ms     = timestamp_ms,
        .temperature_cc   = 0,
        .humidity_permil  = 0,
        .co2_ppm          = 0,
        .valid            = false,
        .failure          = reason,
    };
    if (s_callback) {
        s_callback(&sample);
    }
}

static void sensor_env_task(void *pv)
{
    (void)pv;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint32_t loop = 0;
    uint32_t consecutive_failures = 0;

    ESP_LOGI(TAG, "task started (gpio=%d, stack %u bytes, prio %d)",
             s_data_gpio, (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (int)uxTaskPriorityGet(NULL));

    // Connection-table.md §4.1: wait at least 2000 ms after power-on before
    // the first DHT22 read (sensor needs stable VDD after power-up).
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (;;) {
        uint32_t timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // 1) Drive the host start signal on GPIO2. Bus is held LOW on return.
        drive_start_signal(s_data_gpio);

        // 2) Drain any stale semaphore tokens from a previous (timed-out)
        //    transaction before arming a new one. xSemaphoreTake with 0 tick
        //    wait drains one token per call; loop until none remain. This
        //    guarantees the next RX-done signal we get corresponds to the
        //    rmt_receive() call we are about to issue, not a late callback
        //    from a previous iteration.
        while (xSemaphoreTake(s_rx_done_sem, 0) == pdTRUE) {
            /* discard stale token */
        }

        // 3) Bump the transaction ID and set s_armed_txn BEFORE arming RX.
        //    The ISR reads s_armed_txn at callback time; after the sem take
        //    we check s_last_txn == this_txn. Using uint8_t gives 256
        //    transactions before wrap — far more headroom than the 1
        //    outstanding transaction we ever have.
        //
        //    s_armed_txn is set AFTER the stale-sem drain (step 2) and
        //    BEFORE rmt_receive() (below). Between iterations (after sem
        //    take or timeout) the task clears s_armed_txn to 0, so a late
        //    ISR from a previous transaction sees 0 and ignores the event.
        //
        //    Reset s_last_txn and s_last_num_symbols to impossible values
        //    so a stale ISR that fires after s_armed_txn is set but before
        //    the new ISR cannot produce a false match.
        s_expected_txn++;
        uint8_t this_txn = s_expected_txn;
        s_last_txn         = 0;
        s_last_num_symbols = 0;
        s_armed_txn        = this_txn;   // ISR reads this — NOT s_expected_txn
        esp_err_t rx_ret = rmt_receive(s_rmt_rx_chan, s_rx_buf,
                                       sizeof(s_rx_buf), &s_rx_recv_cfg);
        if (rx_ret != ESP_OK) {
            s_armed_txn = 0;   // arm failed — ISR cannot fire for this txn
            ESP_LOGW(TAG, "rmt_receive: %s", esp_err_to_name(rx_ret));
            gpio_set_level(s_data_gpio, 1);   // release bus to idle
            emit_failure(timestamp_ms, ENV_SENSOR_FAIL_TIMEOUT);
            consecutive_failures++;
            goto feed_wdt;
        }

        // 5) Release the bus (OD output high = high-Z). External ~5.1 kΩ
        //    pull-up brings the line high; sensor responds in ~30 µs.
        gpio_set_level(s_data_gpio, 1);

        // 6) Wait for RX done (≤ 100 ms). On timeout, explicitly disable the
        //    RX channel to terminate any in-progress receive — without this,
        //    a late callback could fire on the NEXT iteration's rmt_receive
        //    and corrupt s_last_num_symbols / give a stale semaphore token.
        //    ESP-IDF v5.4 RMT FSM (rmt_private.h): rmt_receive() requires
        //    fsm==ENABLE and transitions ENABLE→RUN; rmt_disable() transitions
        //    RUN/ENABLE→INIT. rmt_receive() does NOT auto-enable a disabled
        //    channel — calling it with fsm==INIT returns ESP_ERR_INVALID_STATE.
        //    So after rmt_disable() in the timeout branch we MUST call
        //    rmt_enable() (INIT→ENABLE) or every subsequent rmt_receive()
        //    fails permanently.
        if (xSemaphoreTake(s_rx_done_sem,
                           pdMS_TO_TICKS(ENV_RMT_TIMEOUT_MS)) == pdTRUE) {
            // Got a semaphore token. Validate that it belongs to THIS
            // transaction (s_last_txn == this_txn). A late callback from a
            // previous timed-out transaction would have a stale ID.
            uint8_t  seen_txn = s_last_txn;
            size_t   num      = s_last_num_symbols;
            if (seen_txn != this_txn) {
                ESP_LOGW(TAG, "DHT22 late callback discarded "
                         "(seen_txn=%u, expected=%u)",
                         (unsigned)seen_txn, (unsigned)this_txn);
                emit_failure(timestamp_ms, ENV_SENSOR_FAIL_PROTOCOL);
                consecutive_failures++;
                goto feed_wdt;
            }
            // DHT22 parser.
            env_sensor_data_t sample = {
                .timestamp_ms    = timestamp_ms,
                .temperature_cc  = 0,
                .humidity_permil = 0,
                .co2_ppm         = 0,
                .valid           = false,
                .failure         = ENV_SENSOR_FAIL_PROTOCOL,
            };
            sample.failure = parse_rmt_dht22(s_rx_buf, num, &sample);

            if (sample.failure == ENV_SENSOR_OK) {
                consecutive_failures = 0;
                s_diag_printed = false;
                ESP_LOGI(TAG, "DHT22: temp=%d cc, humid=%u permil",
                         (int)sample.temperature_cc,
                         (unsigned)sample.humidity_permil);
            } else {
                // One-shot diagnostic on first failure.
                if (!s_diag_printed) {
                    s_diag_printed = true;
                    const size_t dump_n = (num > 42) ? 42 : num;
                    ESP_LOGW(TAG, "DHT22 parse fail: num=%u symbols, "
                             "dump=%u symbols follow",
                             (unsigned)num, (unsigned)dump_n);
                    for (size_t i = 0; i < dump_n; i++) {
                        ESP_LOGI(TAG, "  [%2u] l0=%u d0=%4u  l1=%u d1=%4u",
                                 (unsigned)i,
                                 (unsigned)s_rx_buf[i].level0,
                                 (unsigned)s_rx_buf[i].duration0,
                                 (unsigned)s_rx_buf[i].level1,
                                 (unsigned)s_rx_buf[i].duration1);
                    }
                }
                consecutive_failures++;
                if (consecutive_failures == ENV_FAIL_THRESHOLD) {
                    ESP_LOGW(TAG, "DHT22 %u consecutive parse failures (driver threshold)",
                             ENV_FAIL_THRESHOLD);
                }
            }
            if (s_callback) {
                s_callback(&sample);
            }
            s_armed_txn = 0;   // transaction complete — ISR must ignore stale callbacks
        } else {
            // Timeout: clear arm flag so any late ISR ignores the callback
            s_armed_txn = 0;
            // RMT did not complete within 100 ms → sensor not responding.
            // Disable the channel to terminate the in-progress receive and
            // prevent a late ISR from corrupting the next transaction.
            ESP_LOGW(TAG, "DHT22 no response within %u ms; disabling RX",
                     ENV_RMT_TIMEOUT_MS);
            // rmt_disable() transitions fsm RUN→INIT, terminating the
            // in-progress receive. Then rmt_enable() transitions fsm
            // INIT→ENABLE so the next rmt_receive() succeeds. Without
            // rmt_enable(), every subsequent rmt_receive() returns
            // ESP_ERR_INVALID_STATE (see FSM note in step 6 above).
            esp_err_t dis_ret = rmt_disable(s_rmt_rx_chan);
            if (dis_ret != ESP_OK) {
                ESP_LOGE(TAG, "rmt_disable after timeout: %s",
                         esp_err_to_name(dis_ret));
            }
            esp_err_t en_ret = rmt_enable(s_rmt_rx_chan);
            if (en_ret != ESP_OK) {
                ESP_LOGE(TAG, "rmt_enable after timeout recovery: %s",
                         esp_err_to_name(en_ret));
            }
            emit_failure(timestamp_ms, ENV_SENSOR_FAIL_TIMEOUT);
            consecutive_failures++;
        }

        feed_wdt:

        if (consecutive_failures >= ENV_FAIL_THRESHOLD) {
            // Log once per offline episode, then suppress until recovery.
            // The state machine (process_env) owns online/offline state.
            if (!s_threshold_logged) {
                s_threshold_logged = true;
                ESP_LOGW(TAG, "DHT22 consecutive failures=%" PRIu32 " (≥ %u)",
                         consecutive_failures, ENV_FAIL_THRESHOLD);
            }
        } else {
            s_threshold_logged = false;
        }

        // Feed TWDT after each sample attempt. Max gap since previous feed =
        // ENV_SAMPLE_PERIOD_MS (5 s) + ≤ 100 ms RMT wait, which stays well
        // under the 10 s TWDT timeout (task-architecture.md §7.1, §7.2).
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop % 12) == 0) {   // ~ every 60 s
            ESP_LOGI(TAG, "heartbeat: loop=%u, fail=%u, stack_hwm=%u bytes",
                     (unsigned)loop, (unsigned)consecutive_failures,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        // Wait for the next 5 s sample period. The start signal + RMT wait
        // consumed ≤ ~100 ms; the remainder is sleep here.
        vTaskDelay(pdMS_TO_TICKS(ENV_SAMPLE_PERIOD_MS));
    }
}

esp_err_t env_sensor_start(gpio_num_t data_gpio,
                           uint32_t rmt_clk_hz,
                           env_sensor_data_callback_t callback)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_data_gpio = data_gpio;
    s_callback  = callback;

    // GPIO2: start as input with internal pull-up disabled (external 5.1 kΩ
    // pull-up is on the board). Direction is toggled per-transaction.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << data_gpio),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(ret));
        return ret;
    }

    // RMT RX channel at the caller-specified resolution (1 MHz → 1 µs tick).
    rmt_rx_channel_config_t rx_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = rmt_clk_hz,
        .mem_block_symbols = ENV_RMT_MEM_SYMBOLS,
        .gpio_num          = data_gpio,
        .flags.invert_in   = false,
        .flags.with_dma    = false,
    };
    ret = rmt_new_rx_channel(&rx_cfg, &s_rmt_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_rx_channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Binary semaphore used by the RX-done ISR callback to wake the task.
    s_rx_done_sem = xSemaphoreCreateBinary();
    if (s_rx_done_sem == NULL) {
        rmt_del_channel(s_rmt_rx_chan);
        s_rmt_rx_chan = NULL;
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = on_rmt_rx_done,
    };
    ret = rmt_rx_register_event_callbacks(s_rmt_rx_chan, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_rx_register_event_callbacks: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_rx_done_sem);
        s_rx_done_sem = NULL;
        rmt_del_channel(s_rmt_rx_chan);
        s_rmt_rx_chan = NULL;
        return ret;
    }

    ret = rmt_enable(s_rmt_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_rx_done_sem);
        s_rx_done_sem = NULL;
        rmt_del_channel(s_rmt_rx_chan);
        s_rmt_rx_chan = NULL;
        return ret;
    }

    if (xTaskCreate(sensor_env_task, "sensor_env", ENV_TASK_STACK,
                    NULL, ENV_TASK_PRIO, &s_task_handle) != pdPASS) {
        rmt_disable(s_rmt_rx_chan);
        rmt_del_channel(s_rmt_rx_chan);
        s_rmt_rx_chan = NULL;
        vSemaphoreDelete(s_rx_done_sem);
        s_rx_done_sem = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "started (gpio=%d, rmt_clk=%u Hz)",
             data_gpio, (unsigned)rmt_clk_hz);
    return ESP_OK;
}

esp_err_t env_sensor_stop(void)
{
    // TODO: delete task, disable + delete RMT channel, delete semaphore,
    //       reset GPIO to input. Clear s_callback.
    return ESP_ERR_NOT_SUPPORTED;
}
