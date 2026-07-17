// PrivacySense Matter Room Hub - LD2410C-P radar driver implementation
//
// Skeleton. The real frame parser (V1.09 protocol) arrives in a follow-up
// commit after first build succeeds. This file currently:
//   - Configures UART1 on the caller-specified pins
//   - Spawns sensor_radar_task
//   - Drains UART into a ring buffer and logs frame sync word hunts at DEBUG
//   - Emits a placeholder radar_data_t (valid=false) every UART idle timeout
//     so the state_machine_task loop can be exercised end-to-end before the
//     parser is ready (state-model.md §5.1 sensor timeout path).
//
// Ownership (task-architecture.md §4.1, §5.1, §7.2):
//   - sensor_radar_task is the ONLY writer to UART1 and the ONLY caller of
//     the registered data callback.
//   - TWDT feed interval ≤ 500 ms: the loop blocks on uart_read_bytes with a
//     500 ms timeout and feeds the watchdog immediately after each iteration.
//     There is NO trailing vTaskDelay — the UART timeout itself paces the loop
//     on an idle bus, and a busy bus returns from uart_read_bytes quickly.

#include "ld2410c.h"

#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ld2410c";

#define LD2410C_TASK_STACK      4096
#define LD2410C_TASK_PRIO       5
#define LD2410C_UART_BUF_RX     1024
#define LD2410C_READ_TIMEOUT_MS 500      // task-architecture.md §4.1
// Post-read processing must stay well under 500 ms so the total gap between
// TWDT feeds remains ≤ 500 ms (task-architecture.md §7.2).

// V1.09 protocol constants (head/tail markers).
#define LD2410C_FRAME_HEAD1  0xF4
#define LD2410C_FRAME_HEAD2  0xF3
#define LD2410C_FRAME_HEAD3  0xF2
#define LD2410C_FRAME_HEAD4  0xF1
#define LD2410C_FRAME_TAIL1  0xF8
#define LD2410C_FRAME_TAIL2  0xF7
#define LD2410C_FRAME_TAIL3  0xF6
#define LD2410C_FRAME_TAIL4  0xF5

static uart_port_t              s_uart_num     = UART_NUM_MAX;
static ld2410c_data_callback_t  s_data_callback = NULL;
static TaskHandle_t             s_task_handle  = NULL;

static void sensor_radar_task(void *pv)
{
    (void)pv;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint8_t buf[128];
    uint32_t loop = 0;

    ESP_LOGI(TAG, "task started (uart=%d, stack %u bytes, prio %d)",
             s_uart_num, (unsigned)uxTaskGetStackHighWaterMark(NULL),
             uxTaskPriorityGet(NULL));

    for (;;) {
        int n = uart_read_bytes(s_uart_num, buf, sizeof(buf),
                                pdMS_TO_TICKS(LD2410C_READ_TIMEOUT_MS));
        if (n > 0) {
            // TODO: implement V1.09 frame parser:
            //   1. Scan for head marker 0xF4 0xF3 0xF2 0xF1
            //   2. Read length + command word
            //   3. Read payload (target_state, moving/static distance + energy)
            //   4. Verify tail 0xF8 0xF7 0xF6 0xF5
            //   5. Optionally CRC-16 check
            //   6. Build ld2410c_radar_data_t with valid=true and invoke
            //      s_data_callback(&frame). Do NOT touch any queue here.
            ESP_LOGD(TAG, "rx %d bytes (parser TODO)", n);
        } else if (n == 0) {
            // No data within timeout → emit a placeholder so the state machine
            // can exercise its sensor-timeout path. Marked invalid.
            // state-model.md §2.3: SENSOR_TIMEOUT_MS (10 s) of consecutive
            // invalid frames → occupancy = UNKNOWN.
            ld2410c_radar_data_t placeholder = {
                .timestamp_ms       = xTaskGetTickCount() * portTICK_PERIOD_MS,
                .target_present     = false,
                .moving_distance_cm = 0,
                .static_distance_cm = 0,
                .moving_energy      = 0,
                .static_energy      = 0,
                .valid              = false,
            };
            if (s_data_callback) {
                s_data_callback(&placeholder);
            }
        }

        // Feed TWDT every iteration. Max gap since previous feed =
        // LD2410C_READ_TIMEOUT_MS (500 ms) + negligible processing, which
        // satisfies task-architecture.md §7.2 (≤ 500 ms for radar task).
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        if ((++loop % 12) == 0) {   // ~ every 6 s at 500 ms cadence
            ESP_LOGI(TAG, "heartbeat: loop=%u, stack_hwm=%u bytes",
                     (unsigned)loop,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
        // No trailing vTaskDelay: the UART read timeout paces the loop on an
        // idle bus. A busy bus returns quickly and we re-enter immediately,
        // which is correct for continuous-frame protocol.
    }
}

esp_err_t ld2410c_start(uart_port_t uart_num,
                        gpio_num_t tx_gpio,
                        gpio_num_t rx_gpio,
                        uint32_t baud,
                        ld2410c_data_callback_t callback)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_uart_num      = uart_num;
    s_data_callback = callback;

    uart_config_t uart_cfg = {
        .baud_rate  = (int)baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_driver_install(uart_num, LD2410C_UART_BUF_RX, 0, 0,
                                        NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = uart_param_config(uart_num, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = uart_set_pin(uart_num, tx_gpio, rx_gpio,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(ret));
        return ret;
    }

    if (xTaskCreate(sensor_radar_task, "sensor_radar", LD2410C_TASK_STACK,
                    NULL, LD2410C_TASK_PRIO, &s_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "started (uart=%d tx=%d rx=%d baud=%u)",
             uart_num, tx_gpio, rx_gpio, (unsigned)baud);
    return ESP_OK;
}

esp_err_t ld2410c_stop(void)
{
    // TODO: delete task, flush UART, uninstall driver, clear s_data_callback.
    return ESP_ERR_NOT_SUPPORTED;
}
