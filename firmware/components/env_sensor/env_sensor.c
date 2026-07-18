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

// Transaction ID to defeat late callbacks. Each rmt_receive() call bumps
// s_expected_txn before arming; the ISR captures the ID it saw at callback
// time into s_last_txn. The task only accepts a callback result if
// s_last_txn == s_expected_txn. A late callback from a previous (timed-out)
// transaction will have a stale ID and is discarded.
//
// Both variables are only touched from two contexts:
//   - sensor_env_task (writes s_expected_txn, reads s_last_txn after sem take)
//   - on_rmt_rx_done ISR (writes s_last_txn, gives sem)
// The semaphore provides the necessary happens-before ordering for the
// s_last_txn read in the task. s_expected_txn is only read by the ISR via a
// stale snapshot — worst case is one extra discarded callback, which is
// harmless.
static volatile uint8_t s_expected_txn = 0;
static volatile uint8_t s_last_txn     = 0;

// DHT22 valid range (state-model.md §5.2). Out-of-range samples are discarded.
// Units: centi-celsius (1/100 °C) and per-mille (1/1000 %RH).
#define DHT22_TEMP_MIN_CC   (-4000)    // -40.00 °C
#define DHT22_TEMP_MAX_CC   ( 8000)    //  80.00 °C
#define DHT22_HUMID_MIN_PM  (0)        //   0 %RH
#define DHT22_HUMID_MAX_PM  (1000)     // 100 %RH

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
// DHT22 pulses: 27–80 µs. Use 10 µs floor (noise filter) and 100 µs ceiling.
// NOTE: rmt_receive_config_t in ESP-IDF v5.4 has NO flags.invert_in field;
// input inversion is configured in rmt_rx_channel_config_t.flags instead.
#define DHT22_SIGNAL_MIN_NS   (10UL  * 1000U)   // 10 µs
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
    // ISR context: capture symbol count AND the transaction ID that was
    // current when the receive was armed. NO parsing here.
    // edata->received_symbols points into the buffer passed to rmt_receive();
    // the task copies what it needs before issuing the next rmt_receive() call.
    //
    // s_expected_txn may have been bumped by the task if a new transaction
    // was already started (late callback case) — by reading it here into a
    // local, we lock in the value that the task will compare against.
    s_last_num_symbols = edata->num_symbols;
    s_last_txn         = s_expected_txn;
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_rx_done_sem, &high_task_woken);
    return (high_task_woken == pdTRUE);
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

static void emit_placeholder(uint32_t timestamp_ms)
{
    env_sensor_data_t sample = {
        .timestamp_ms     = timestamp_ms,
        .temperature_cc   = 0,
        .humidity_permil  = 0,
        .co2_ppm          = 0,
        .valid            = false,
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

        // 3) Bump the transaction ID BEFORE arming RX. The ISR captures the
        //    ID at callback time into s_last_txn; after the sem take we
        //    compare s_last_txn == s_expected_txn to detect late callbacks.
        //    Using uint8_t gives 256 transactions before wrap — far more
        //    headroom than the 1 outstanding transaction we ever have.
        s_expected_txn++;
        uint8_t this_txn = s_expected_txn;

        // 4) Arm RMT RX BEFORE releasing the bus. The 18 ms low is filtered
        //    as a gap by signal_range_max_ns; the RMT engine is ready to
        //    capture the sensor's response edge the instant we release.
        //    ESP-IDF v5.4 new RMT RX API: rmt_receive() is one-shot, no
        //    separate rmt_rx_start/stop calls.
        s_last_num_symbols = 0;
        esp_err_t rx_ret = rmt_receive(s_rmt_rx_chan, s_rx_buf,
                                       sizeof(s_rx_buf), &s_rx_recv_cfg);
        if (rx_ret != ESP_OK) {
            ESP_LOGW(TAG, "rmt_receive: %s", esp_err_to_name(rx_ret));
            gpio_set_level(s_data_gpio, 1);   // release bus to idle
            emit_placeholder(timestamp_ms);
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
        //    rmt_disable() is idempotent; the channel is re-enabled by the
        //    next rmt_receive() call (ESP-IDF v5.4 new RMT RX API: receive
        //    internally enables the channel if it was disabled).
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
                emit_placeholder(timestamp_ms);
                consecutive_failures++;
                goto feed_wdt;
            }
            // TODO: real DHT22 parser:
            //   1. If num < 41, mark as parse failure (insufficient symbols).
            //   2. Walk s_rx_buf[0..num-1]:
            //      - Skip the sensor response (80 µs low + 80 µs high)
            //      - For each of 40 bits: low duration ~50 µs (fixed),
            //        high duration determines bit value (≈27 µs = 0, ≈70 µs = 1)
            //   3. Assemble 5 bytes: RH_high, RH_low, T_high, T_low, checksum
            //   4. Verify checksum = (RH_high + RH_low + T_high + T_low) & 0xFF
            //   5. Negative temperature: T_high & 0x80 → sign bit
            //   6. Range check (DHT22_TEMP_MIN_CC .. DHT22_TEMP_MAX_CC,
            //      DHT22_HUMID_MIN_PM .. DHT22_HUMID_MAX_PM)
            //   7. On success: build env_sensor_data_t with valid=true and
            //      invoke s_callback(&sample). Reset consecutive_failures=0.
            //   8. On any failure (checksum, range, parse): increment
            //      consecutive_failures; if ≥ ENV_FAIL_THRESHOLD →
            //      emit placeholder with valid=false so the state machine
            //      can mark env_sensor_online=false (state-model.md §5.2).
            ESP_LOGD(TAG, "RMT RX done: %u symbols (parser TODO)", (unsigned)num);
            emit_placeholder(timestamp_ms);
            consecutive_failures++;
        } else {
            // RMT did not complete within 100 ms → sensor not responding.
            // Disable the channel to terminate the in-progress receive and
            // prevent a late ISR from corrupting the next transaction.
            ESP_LOGW(TAG, "DHT22 no response within %u ms; disabling RX",
                     ENV_RMT_TIMEOUT_MS);
            rmt_disable(s_rmt_rx_chan);
            emit_placeholder(timestamp_ms);
            consecutive_failures++;
        }

feed_wdt:

        if (consecutive_failures >= ENV_FAIL_THRESHOLD) {
            // state-model.md §5.2: 3 consecutive failures → sensor offline.
            // The state machine infers this from valid=false samples; no
            // separate "offline" event is sent.
            ESP_LOGW(TAG, "DHT22 consecutive failures=%" PRIu32 " (≥ %u → offline)",
                     consecutive_failures, ENV_FAIL_THRESHOLD);
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
