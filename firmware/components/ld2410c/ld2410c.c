// PrivacySense Matter Room Hub - LD2410C-P radar driver implementation
//
// Implements the V1.09 normal-mode continuous reporting frame parser and
// command-mode configuration protocol.
//
// Normal mode data frame (V1.09):  [F4 F3 F2 F1] ... [F8 F7 F6 F5], 23 bytes.
// Command/ACK frame (V1.09):       [FD FC FB FA] len cmd [status] data [04 03 02 01].
// No SUM/XOR checksum. Validation by head + length + tail.
//
// Architecture (Reviewer P0 #1/#2, task-architecture.md §4.1, §5.1, §7.2):
//   - sensor_radar_task is the SOLE writer to UART1 and owns the UART driver
//     for its whole lifetime. It executes config commands IN-TASK via the
//     pure-C transaction core (ld2410c_core.c). The UART is NEVER deleted
//     during a config transaction.
//   - Command requests travel through s_req_queue BY VALUE (no caller-stack
//     pointers, no caller semaphore). The task fills a response in s_resp_queue
//     BY VALUE and the caller copies it out. This guarantees result propagation
//     and rules out use-after-free on timeout.
//   - s_txn_mutex serialises transactions AND excludes ld2410c_stop(), so a
//     config transaction can never race teardown.

#include "ld2410c.h"
#include "ld2410c_parser.h"
#include "ld2410c_core.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "ld2410c";

#define LD2410C_TASK_STACK      4096
#define LD2410C_TASK_PRIO       5
#define LD2410C_UART_BUF_RX     1024
#define LD2410C_READ_TIMEOUT_MS 500
#define LD2410C_STALE_IDLE_LIMIT 3
#define LD2410C_HEX_DUMP_MAX    0

// ── Internal command request / response (BY VALUE, driver-owned) ───────────
// No pointers to caller memory, no caller semaphore: safe to copy through a
// queue and to drop on timeout with no dangling references.
typedef struct {
    uint32_t transaction_id;
    uint16_t cmd_word;
    int      tx_len;
    uint8_t  tx_data[LD2410C_CMD_MAX_DATA];
} command_request_t;

typedef struct {
    uint32_t transaction_id;
    esp_err_t result;
    int      rx_len;
    uint8_t  rx_data[LD2410C_CMD_MAX_DATA];
} command_response_t;

// ── Static state ──────────────────────────────────────────────────────────
static uart_port_t              s_uart_num     = UART_NUM_MAX;
static int                      s_pin_tx       = -1;
static int                      s_pin_rx       = -1;
static ld2410c_data_callback_t  s_data_callback = NULL;
static TaskHandle_t             s_task_handle  = NULL;

static QueueHandle_t            s_req_queue    = NULL;
static QueueHandle_t            s_resp_queue   = NULL;
static SemaphoreHandle_t        s_txn_mutex    = NULL;
static StaticSemaphore_t        s_txn_mutex_buf;

static uint32_t                 s_txn_id       = 0;
static volatile bool            s_stopping     = false;
// s_stop_request replaced with event-group bit for cross-task signalling
// (Reviewer P1 sync).  The radar task polls the bit via xEventGroupGetBits
// in its main loop; stop() sets it via xEventGroupSetBits.
// s_radar_state_uncertain moved to s_event_group UNCERTAIN_BIT
// for cross-task synchronisation (Reviewer P1).

static SemaphoreHandle_t        s_stop_done    = NULL;
static StaticSemaphore_t        s_stop_done_buf;

static EventGroupHandle_t       s_event_group  = NULL;
static StaticEventGroup_t       s_event_group_buf;
#define STOP_BIT                (1 << 0)
#define UNCERTAIN_BIT           (1 << 1)

// Rolling byte buffer for normal-mode frame assembly
static uint8_t  s_rx_buf[LD2410C_RX_BUF_CAP];
static int      s_rx_len = 0;
static int      s_idle_count = 0;

#if LD2410C_HEX_DUMP_MAX > 0
static uint32_t s_hex_dump_count = 0;
static bool     s_hex_dumped     = false;
#endif

// ── helpers ────────────────────────────────────────────────────────────────

static void emit_proto_error(void)
{
    ld2410c_radar_data_t err = {
        .timestamp_ms       = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .target_present     = false,
        .moving_distance_cm = 0,
        .static_distance_cm = 0,
        .moving_energy      = 0,
        .static_energy      = 0,
        .valid              = false,
        .failure            = LD2410C_FAIL_PROTOCOL,
    };
    if (s_data_callback) s_data_callback(&err);
}

#if LD2410C_HEX_DUMP_MAX > 0
static void hex_dump_raw(const uint8_t *buf, int len) { /* ... unchanged */ }
#else
static inline void hex_dump_raw(const uint8_t *buf, int len) { (void)buf; (void)len; }
#endif

static uint32_t radar_clock_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// ── UART transport for the transaction core ────────────────────────────────
static int uart_send_fn(void *ctx, const uint8_t *buf, int len)
{
    uart_port_t u = (uart_port_t)(intptr_t)ctx;
    return uart_write_bytes(u, buf, len);
}
static int uart_recv_fn(void *ctx, uint8_t *buf, int cap, int timeout_ms)
{
    uart_port_t u = (uart_port_t)(intptr_t)ctx;
    return uart_read_bytes(u, buf, cap, pdMS_TO_TICKS(timeout_ms));
}
static void uart_flush_fn(void *ctx)
{
    uart_port_t u = (uart_port_t)(intptr_t)ctx;
    uart_flush_input(u);
}

// ── normal-mode RX drain ──────────────────────────────────────────────────
static void drain_rx_buf(void)
{
    while (s_rx_len >= LD2410C_FRAME_HEAD_LEN + LD2410C_FRAME_LEN_FIELD) {
        int head_off = -1;
        for (int i = 0; i <= s_rx_len - 4; i++) {
            if (ld2410c_is_head(s_rx_buf + i)) {
                head_off = i;
                break;
            }
        }
        if (head_off < 0) {
            int keep = (s_rx_len > 3) ? 3 : s_rx_len;
            if (keep < s_rx_len) {
                memmove(s_rx_buf, s_rx_buf + s_rx_len - keep, keep);
            }
            s_rx_len = keep;
            return;
        }
        if (head_off > 0) {
            memmove(s_rx_buf, s_rx_buf + head_off, s_rx_len - head_off);
            s_rx_len -= head_off;
        }
        int total = ld2410c_frame_total_size(s_rx_buf, s_rx_len);
        if (total == 0) return;
        if (total < 0) {
            emit_proto_error();
            memmove(s_rx_buf, s_rx_buf + 1, s_rx_len - 1);
            s_rx_len--;
            continue;
        }
        ld2410c_radar_data_t frame = { .timestamp_ms = 0 };
        if (ld2410c_try_parse_frame(s_rx_buf, s_rx_len, &frame)) {
            frame.timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (s_data_callback) s_data_callback(&frame);
            int remain = s_rx_len - total;
            if (remain > 0) memmove(s_rx_buf, s_rx_buf + total, remain);
            s_rx_len = remain;
        } else {
            emit_proto_error();
            memmove(s_rx_buf, s_rx_buf + 1, s_rx_len - 1);
            s_rx_len--;
        }
    }
}

// ── task ──────────────────────────────────────────────────────────────────
static void sensor_radar_task(void *pv)
{
    (void)pv;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint8_t temp_buf[128];
    uint32_t loop = 0;

    ESP_LOGI(TAG, "task started (uart=%d, stack %u bytes, prio %d)",
             s_uart_num, (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (int)uxTaskPriorityGet(NULL));

    for (;;) {
        int n = uart_read_bytes(s_uart_num, temp_buf, sizeof(temp_buf),
                                pdMS_TO_TICKS(LD2410C_READ_TIMEOUT_MS));

        if (xEventGroupGetBits(s_event_group) & STOP_BIT) {
#if defined(LD2410C_TEST_FAULT_INJECT_STOP)
            // R08: simulate task that does not exit immediately.
            // First stop() will timeout (1100 ms > 1000 ms wait).
            // Second stop() finds s_stop_done already given → succeeds.
            // Log string must match tests/evidence/R08_stop_timeout_fault_inject_test.md
            ESP_LOGW(TAG, "FAULT INJECT: sleeping 1100 ms to force timeout");
            vTaskDelay(pdMS_TO_TICKS(1100));
#endif
            break;
        }

        // Check for a pending config request (0-wait poll).
        command_request_t req;
        if (xQueueReceive(s_req_queue, &req, 0) == pdTRUE) {
            command_response_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.transaction_id = req.transaction_id;

            ld2410c_transport_t t = {
                .send  = uart_send_fn,
                .recv  = uart_recv_fn,
                .flush = uart_flush_fn,
                .ctx   = (void *)(intptr_t)s_uart_num,
            };
            ld2410c_transaction_detail_t det;
            esp_err_t r = ld2410c_core_exec_transaction(
                &t, radar_clock_ms, req.cmd_word,
                req.tx_data, req.tx_len,
                resp.rx_data, sizeof(resp.rx_data), &resp.rx_len,
                LD2410C_CMD_TIMEOUT_MS, &det);
            resp.result = r;

            // Determine uncertain state from per-step results.
            // ESP_ERR_TIMEOUT or ESP_FAIL: sent command but no valid ACK or
            // transport error — radar may have entered config mode despite
            // the error (Reviewer P0).  Only ESP_ERR_INVALID_RESPONSE (ACK
            // received with non-zero status) proves enable did NOT enter config.
            // Written via event-group bit for cross-task sync (Reviewer P1).
            if (det.enable_result == ESP_ERR_INVALID_RESPONSE) {
                xEventGroupClearBits(s_event_group, UNCERTAIN_BIT);
            } else if (det.enable_result != ESP_OK) {
                xEventGroupSetBits(s_event_group, UNCERTAIN_BIT);
                ESP_LOGW(TAG, "config transaction: enable %s → UNCERTAIN",
                         esp_err_to_name(det.enable_result));
            } else if (det.disable_result != ESP_OK) {
                xEventGroupSetBits(s_event_group, UNCERTAIN_BIT);
                ESP_LOGW(TAG, "config transaction: disable %s → UNCERTAIN",
                         esp_err_to_name(det.disable_result));
            } else if (det.business_result == ESP_OK) {
                xEventGroupClearBits(s_event_group, UNCERTAIN_BIT);
            } else {
                // biz failed (enable=OK, disable=OK): clean exit from config
                xEventGroupClearBits(s_event_group, UNCERTAIN_BIT);
            }
            if (xQueueOverwrite(s_resp_queue, &resp) != pdTRUE) {
                ESP_LOGE(TAG, "resp queue overwrite failed");
            }
            continue;
        }

        if (n > 0) {
            s_idle_count = 0;
            hex_dump_raw(temp_buf, n);

            int room = (int)sizeof(s_rx_buf) - s_rx_len;
            if (n > room) {
                int discard = n - room;
                if (discard >= s_rx_len) {
                    s_rx_len = 0;
                } else {
                    memmove(s_rx_buf, s_rx_buf + discard, s_rx_len - discard);
                    s_rx_len -= discard;
                }
                room = (int)sizeof(s_rx_buf) - s_rx_len;
                if (room < 0) room = 0;
            }
            int copy = (n <= room) ? n : room;
            memcpy(s_rx_buf + s_rx_len, temp_buf, copy);
            s_rx_len += copy;
            drain_rx_buf();
        } else if (n == 0) {
            if (s_rx_len > 0) {
                s_idle_count++;
                if (s_idle_count >= LD2410C_STALE_IDLE_LIMIT) {
                    ESP_LOGD(TAG, "stale timeout, discarding %d bytes", s_rx_len);
                    s_rx_len = 0;
                    s_idle_count = 0;
                }
            } else {
                ld2410c_radar_data_t placeholder = {
                    .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
                    .target_present = false,
                    .valid = false,
                    .failure = LD2410C_FAIL_TIMEOUT,
                };
                if (s_data_callback) s_data_callback(&placeholder);
            }
        }

        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop % 12) == 0) {
            ESP_LOGI(TAG, "heartbeat: loop=%u, stack_hwm=%u, rx=%d",
                     (unsigned)loop,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL),
                     s_rx_len);
        }
    }

    esp_task_wdt_delete(NULL);
    xSemaphoreGive(s_stop_done);
    vTaskDelete(NULL);
}

// ── lifecycle ──────────────────────────────────────────────────────────────

static void release_uart(void)
{
    if (s_uart_num != UART_NUM_MAX) {
        uart_driver_delete(s_uart_num);
        if (s_pin_tx >= 0) gpio_reset_pin((gpio_num_t)s_pin_tx);
        if (s_pin_rx >= 0) gpio_reset_pin((gpio_num_t)s_pin_rx);
        s_pin_tx = -1;
        s_pin_rx = -1;
    }
}

esp_err_t ld2410c_start(uart_port_t uart_num,
                        gpio_num_t tx_gpio,
                        gpio_num_t rx_gpio,
                        uint32_t baud,
                        ld2410c_data_callback_t callback)
{
    if (callback == NULL) return ESP_ERR_INVALID_ARG;

    // One-time initialisation of synchronisation primitives (MUST happen
    // before any xSemaphoreTake — Reviewer P0 fix for NULL-mutex crash
    // on first boot).
    if (s_stop_done == NULL) {
        s_stop_done = xSemaphoreCreateBinaryStatic(&s_stop_done_buf);
    }
    if (s_txn_mutex == NULL) {
        s_txn_mutex = xSemaphoreCreateMutexStatic(&s_txn_mutex_buf);
    }
    if (s_event_group == NULL) {
        s_event_group = xEventGroupCreateStatic(&s_event_group_buf);
    }

    // Serialise with stop() via s_txn_mutex (Reviewer P0-2).
    if (xSemaphoreTake(s_txn_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_task_handle != NULL || s_stopping) {
        xSemaphoreGive(s_txn_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Only clear lifecycle flags AFTER confirming state is clean — doing so
    // before the mutex/state check would cancel an in-progress stop() by
    // clearing its STOP_BIT (Reviewer P0).
    xEventGroupClearBits(s_event_group, STOP_BIT | UNCERTAIN_BIT);

    s_uart_num      = uart_num;
    s_pin_tx        = (int)tx_gpio;
    s_pin_rx        = (int)rx_gpio;
    s_data_callback = callback;
    s_rx_len        = 0;
    s_idle_count    = 0;
    s_stopping      = false;
    s_txn_id        = 0;

    s_req_queue = xQueueCreate(1, sizeof(command_request_t));
    if (s_req_queue == NULL) {
        xSemaphoreGive(s_txn_mutex);
        s_uart_num = UART_NUM_MAX;
        return ESP_ERR_NO_MEM;
    }
    s_resp_queue = xQueueCreate(1, sizeof(command_response_t));
    if (s_resp_queue == NULL) {
        vQueueDelete(s_req_queue);
        s_req_queue = NULL;
        xSemaphoreGive(s_txn_mutex);
        s_uart_num = UART_NUM_MAX;
        return ESP_ERR_NO_MEM;
    }

    uart_config_t uart_cfg = {
        .baud_rate  = (int)baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(uart_num, LD2410C_UART_BUF_RX,
                                        0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_txn_mutex);
        goto fail_queues;
    }
    ret = uart_param_config(uart_num, &uart_cfg);
    if (ret != ESP_OK) { release_uart(); xSemaphoreGive(s_txn_mutex); goto fail_queues; }
    ret = uart_set_pin(uart_num, tx_gpio, rx_gpio,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { release_uart(); xSemaphoreGive(s_txn_mutex); goto fail_queues; }

    if (xTaskCreate(sensor_radar_task, "sensor_radar", LD2410C_TASK_STACK,
                    NULL, LD2410C_TASK_PRIO, &s_task_handle) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "xTaskCreate failed");
        release_uart();
        xSemaphoreGive(s_txn_mutex);
        goto fail_queues;
    }

    xSemaphoreGive(s_txn_mutex);
    ESP_LOGI(TAG, "started (uart=%d tx=%d rx=%d baud=%u)",
             uart_num, tx_gpio, rx_gpio, (unsigned)baud);
    return ESP_OK;

fail_queues:
    if (s_req_queue)  { vQueueDelete(s_req_queue);  s_req_queue = NULL; }
    if (s_resp_queue) { vQueueDelete(s_resp_queue); s_resp_queue = NULL; }
    s_uart_num = UART_NUM_MAX;
    return ret;
}

esp_err_t ld2410c_stop(void)
{
    if (s_task_handle == NULL) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_txn_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "stop: could not take txn mutex within 2 s");
        return ESP_ERR_TIMEOUT;
    }

    if (!s_stopping) {
        s_stopping     = true;
        xEventGroupSetBits(s_event_group, STOP_BIT);
    }
    xSemaphoreGive(s_txn_mutex);

    if (xSemaphoreTake(s_stop_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "stop: task did not stop within 1 s; "
                 "retry stop() to complete teardown");
        return ESP_ERR_TIMEOUT;
    }

    // Task has exited — re-acquire mutex for cleanup.
    xSemaphoreTake(s_txn_mutex, portMAX_DELAY);
    uart_flush_input(s_uart_num);
    release_uart();
    if (s_req_queue)  { vQueueDelete(s_req_queue);  s_req_queue = NULL; }
    if (s_resp_queue) { vQueueDelete(s_resp_queue); s_resp_queue = NULL; }
    // Clear handle only AFTER all resources released so concurrent
    // start() sees s_task_handle != NULL while cleanup runs (Reviewer P0-2).
    s_task_handle = NULL;
    s_data_callback = NULL;
    s_rx_len = 0;
    s_idle_count = 0;
    s_stopping = false;
    s_uart_num = UART_NUM_MAX;
    xSemaphoreGive(s_txn_mutex);

    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

// ── public config API ──────────────────────────────────────────────────────
// Synchronous, blocking. Serialised by s_txn_mutex; rejected while stopping.
esp_err_t ld2410c_exec_cmd(uint16_t cmd_word,
                           const uint8_t *tx_data, int tx_data_len,
                           uint8_t *rx_buf, int rx_cap, int *rx_out_len)
{
    if (s_task_handle == NULL)    return ESP_ERR_INVALID_STATE;
    if (s_req_queue == NULL || s_resp_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (rx_buf == NULL && rx_cap > 0) return ESP_ERR_INVALID_ARG;
    if (rx_cap < 0)               return ESP_ERR_INVALID_ARG;
    if (rx_out_len == NULL)       return ESP_ERR_INVALID_ARG;
    if (tx_data_len < 0)          return ESP_ERR_INVALID_ARG;
    if (tx_data_len > LD2410C_CMD_MAX_DATA) return ESP_ERR_INVALID_ARG;
    // tx_data_len > 0 with NULL tx_data silently sends zeros — reject
    if (tx_data_len > 0 && tx_data == NULL) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(s_txn_mutex,
                       pdMS_TO_TICKS(LD2410C_CMD_TIMEOUT_MS * 3 + 2000))
            != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    // Re-check state after taking mutex — stop() may have run while we waited.
    if (s_stopping || s_task_handle == NULL ||
        s_req_queue == NULL || s_resp_queue == NULL) {
        xSemaphoreGive(s_txn_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    command_request_t req;
    memset(&req, 0, sizeof(req));
    req.transaction_id = ++s_txn_id;
    req.cmd_word = cmd_word;
    req.tx_len = tx_data_len;
    if (tx_data != NULL && tx_data_len > 0) {
        memcpy(req.tx_data, tx_data, tx_data_len);
    }

    esp_err_t result = ESP_ERR_TIMEOUT;
    if (xQueueSend(s_req_queue, &req, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Loop discarding stale/mismatched responses until we get the right
        // transaction_id or timeout.  Uses elapsed-time comparison so that
        // tick counter wrap-around is handled safely via unsigned subtraction
        // (Reviewer P0 tick safety).
        TickType_t start     = xTaskGetTickCount();
        TickType_t resp_timeout = pdMS_TO_TICKS(LD2410C_CMD_TIMEOUT_MS * 3 + 1000);
        command_response_t resp;
        while (1) {
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (elapsed >= resp_timeout) break;
            TickType_t rem = resp_timeout - elapsed;
            if (xQueueReceive(s_resp_queue, &resp, rem) != pdTRUE) break;
            if (resp.transaction_id == req.transaction_id) {
                if (resp.rx_len > rx_cap) {
                    result = ESP_ERR_INVALID_SIZE;
                    break;
                }
                if (resp.rx_len > 0 && rx_buf != NULL) {
                    memcpy(rx_buf, resp.rx_data, resp.rx_len);
                }
                *rx_out_len = resp.rx_len;
                result = resp.result;
                break;
            }
            // Mismatched ID — discard and continue waiting for ours.
        }
    }

    xSemaphoreGive(s_txn_mutex);
    return result;
}

// ── type-safe high-level API (Reviewer P0 #5) ──────────────────────────────

esp_err_t ld2410c_read_params(ld2410c_read_params_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t rx[LD2410C_CMD_MAX_DATA];
    int rx_len = 0;
    esp_err_t r = ld2410c_exec_cmd(LD2410C_CMD_READ_PARAMS, NULL, 0,
                                   rx, sizeof(rx), &rx_len);
    if (r != ESP_OK) return r;
    if (!ld2410c_parse_read_params_payload(rx, rx_len, out)) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t ld2410c_write_basic_params(const ld2410c_basic_params_t *params)
{
    if (params == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    if (ld2410c_build_write_basic_params(frame, sizeof(frame), &len, params)
            == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int data_off = LD2410C_CMD_HEAD_LEN + LD2410C_CMD_LEN_FIELD
                   + LD2410C_CMD_WORD_LEN;
    int data_len = len - data_off - LD2410C_CMD_TAIL_LEN;
    const uint8_t *data = frame + data_off;

    int rx_len = 0;
    return ld2410c_exec_cmd(LD2410C_CMD_WRITE_PARAMS, data, data_len,
                            NULL, 0, &rx_len);
}

esp_err_t ld2410c_set_gate_sensitivity(uint16_t gate,
                                       uint8_t moving, uint8_t stationary)
{
    uint8_t frame[LD2410C_CMD_MAX_FRAME];
    int len;
    if (ld2410c_build_set_sensitivity(frame, sizeof(frame), &len,
                                      gate, moving, stationary) == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int data_off = LD2410C_CMD_HEAD_LEN + LD2410C_CMD_LEN_FIELD
                   + LD2410C_CMD_WORD_LEN;
    int data_len = len - data_off - LD2410C_CMD_TAIL_LEN;
    const uint8_t *data = frame + data_off;

    int rx_len = 0;
    return ld2410c_exec_cmd(LD2410C_CMD_SET_SENS, data, data_len,
                            NULL, 0, &rx_len);
}

bool ld2410c_radar_state_uncertain(void)
{
    if (s_event_group == NULL) return false;
    return xEventGroupGetBits(s_event_group) & UNCERTAIN_BIT;
}
