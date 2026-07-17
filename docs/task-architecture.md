# 任务架构

## 1. 设计目标

定义固件中所有 FreeRTOS 任务的职责边界、队列/事件流、优先级、栈空间、阻塞上限和共享资源所有权，为后续核验 watchdog、堆和栈余量提供依据。

## 2. 任务总表

| 任务名 | 优先级 | 栈大小 | 周期/触发 | 阻塞上限 | 职责 | Watchdog |
|---|---|---|---|---|---|---|
| `sensor_radar_task` | 5 (中高) | 4096 B | UART 连续接收 + 200 ms 状态评估 | 500 ms | 读取 LD2410C UART 数据，解析，发送事件到 `app_event_queue` | 启用 |
| `sensor_env_task` | 4 (中) | 4096 B | 5 s 轮询 | 100 ms | 通过 GPIO2/RMT 读取 DHT22，校验并发送事件到 `app_event_queue` | 启用 |
| `state_machine_task` | 6 (高) | 6144 B | 事件驱动 (`app_event_queue`) + 1 s 周期 | 2 s | 消费统一事件队列，评估状态转换，更新 `room_state` | 启用 |
| `button_task` | 5 (中高) | 2048 B | 事件驱动 (ISR → 队列) | 无 (阻塞读队列) | 按键消抖，短按/长按检测，发送事件到 `app_event_queue` | 启用 |
| `ui_task` | 3 (中低) | 3072 B | 200 ms 周期 | 500 ms | 更新 RGB LED 和可选 OLED | 启用 |
| `network_task` | 4 (中) | 8192 B | 事件驱动 (Wi-Fi 事件) | 无 (阻塞读事件) | 管理 Wi-Fi 连接、重连、退避，发送网络状态事件到 `app_event_queue` | 启用 |
| `matter_adapter_task` | 4 (中) | 12288 B | 事件驱动 (Matter 回调) | 无 (Matter 事件循环) | 作为 ESP-Matter 内部 CHIP 任务的事件适配器，将 Matter 事件转换为 `app_event_t` 发送到 `app_event_queue`，将 `room_state` 变更映射为 Matter 属性上报 | 启用 |
| `config_task` | 2 (低) | 4096 B | 事件驱动 (配置变更) | 5 s | NVS 读写、配置版本迁移 | 启用 |

> **优先级说明**：ESP-IDF 默认优先级范围为 0–24，实际使用 0–6。高优先级任务抢占低优先级任务。`state_machine_task` 为最高业务优先级，确保状态转换及时。
>
> **重要变更**：
> - 不再使用多个独立队列（`radar_data_queue`、`env_data_queue`、`button_event_queue`），统一为单一 `app_event_queue`。
> - `matter_adapter_task` 不是独立的 Matter event loop，而是 ESP-Matter 内部 CHIP 任务的事件适配器，负责 Matter 事件与本地事件之间的转换。

## 3. 数据流图

```text
                    ┌─────────────────┐
                    │  LD2410C (UART)  │
                    └────────┬────────┘
                             │ app_event_t (RADAR_DATA)
                             ▼
┌──────────────┐    ┌────────────────┐    ┌─────────────────────┐
│ button ISR   │───▶│ button_task     │    │ sensor_radar_task   │
│ (GPIO 9)     │    │ (debounce)      │    │ (UART poll + parse) │
└──────────────┘    └───────┬────────┘    └──────────┬──────────┘
                            │                        │
                     app_event_t              app_event_t
                      (BUTTON)                 (RADAR_DATA)
                            │                        │
                            ▼                        ▼
┌─────────────────┐  ┌───────────────────────────────────┐
│ AM2302/DHT22    │  │         app_event_queue            │
│ (GPIO2 + RMT)   │  │      (统一事件队列, 深度 32)       │
└────────┬────────┘  └───────────────┬───────────────────┘
         │ app_event_t               ▲
         │ (ENV_DATA)                │
         ▼                           │
┌──────────────────┐                 │
│ sensor_env_task   │                 │
│ (RMT capture)     │                 │
└────────┬─────────┘                 │
         │ app_event_t               │
         │ (ENV_DATA)                │
         ▼                           │
┌──────────────────┐    ┌─────────────────────┐
│ network_task      │───▶│ app_event_t         │
│ (Wi-Fi events)    │    │ (NETWORK_STATUS)    │
└──────────────────┘    └─────────────────────┘
                         │
┌──────────────────┐    ┌─────────────────────┐
│ matter_adapter_  │───▶│ app_event_t         │
│ task             │    │ (MATTER_*)          │
└──────────────────┘    └─────────────────────┘

                    ┌───────────────────────────────────┐
                    │       state_machine_task           │
                    │  (消费 app_event_queue 单点处理)    │
                    └───────────────┬───────────────────┘
                                    │
                            room_state_t (mutex)
                                    │
                    ┌───────────────┼───────────────┐
                    │               │               │
                    ▼               ▼               ▼
           ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
           │  ui_task     │ │ matter_adapter│ │ config_task  │
           │ (RGB/OLED)   │ │ (属性上报)    │ │ (NVS R/W)    │
           └──────────────┘ └──────────────┘ └──────────────┘
```

## 4. 任务职责详述

### 4.1 sensor_radar_task

| 项 | 说明 |
|---|---|
| 职责 | 通过 UART1 连续接收 LD2410C 正常模式数据帧，提取占用信息并发送 `EVENT_RADAR_DATA`；工程模式只用于标定 |
| 输入 | UART1 接收数据 |
| 输出 | `app_event_queue` 中的 `EVENT_RADAR_DATA` |
| 周期 | UART 连续接收；每 200 ms 至少完成一次状态评估 |
| 阻塞点 | UART 读取超时 500 ms |
| 禁止事项 | 不得在任务中直接修改 `room_state`；不得执行 I2C 操作；不得分配动态内存 |
| 栈依据 | UART 缓冲区解析 ~2 KB + 函数调用栈 ~1 KB + 余量 |

### 4.2 sensor_env_task

| 项 | 说明 |
|---|---|
| 职责 | 驱动 GPIO2 起始信号并使用 RMT RX 捕获 DHT22 脉宽，解析 40-bit 数据、校验 checksum 和范围后发送 `EVENT_ENV_DATA` |
| 输入 | GPIO2 单总线数据；约 5.1 kΩ 上拉至 3.3 V |
| 输出 | `app_event_queue` 中的 `EVENT_ENV_DATA`；连续失败计数和 `env_sensor_online` 状态事件 |
| 周期 | 5 s 轮询 |
| 阻塞点 | RMT 接收/协议事务总超时 100 ms；不得因传感器缺失阻塞到下一周期 |
| 禁止事项 | 不得直接修改 `room_state`；不得执行 UART/I2C；不得在 ISR 中解析；不得用长临界区或全程忙等实现微秒时序 |
| 栈依据 | RMT 接收缓冲和解析 ~2 KB + 数据转换/日志 ~1 KB + 余量；首次运行以高水位实测调整 |

### 4.3 state_machine_task

| 项 | 说明 |
|---|---|
| 职责 | 消费 `app_event_queue` 中的所有事件类型（雷达、环境、按键、网络、Matter），评估三个维度的状态转换，更新 `room_state`（持锁），触发 Matter 属性上报 |
| 输入 | `app_event_queue`（统一事件队列） |
| 输出 | `room_state_t`（通过 mutex 保护），`matter_report_queue` 中的属性同步请求，`config_event_queue` 中的持久化请求 |
| 周期 | 事件驱动（`xQueueReceive` 阻塞读，超时 1 s 触发周期检查如 NIGHT 时段判定） |
| 阻塞点 | `xQueueReceive` 超时 1 s；`room_state_mutex` 持锁上限 10 ms |
| 禁止事项 | 不得执行 UART/I2C 操作；不得直接操作 RGB LED；不得执行网络请求 |
| 栈依据 | 状态机逻辑 ~3 KB + 队列操作 ~1 KB + 余量 |

### 4.4 button_task

| 项 | 说明 |
|---|---|
| 职责 | 接收 GPIO ISR 发送的按键事件，执行软件消抖（50 ms），判定短按/长按，发送 `app_event_t` 到 `app_event_queue` |
| 输入 | `gpio_evt_queue`（来自 ISR） |
| 输出 | `app_event_t`（类型 `EVENT_BUTTON`）到 `app_event_queue` |
| 周期 | 事件驱动 |
| 阻塞点 | `xQueueReceive` 阻塞读（无超时） |
| 禁止事项 | 不得在 ISR 中执行消抖逻辑（ISR 只发送队列） |
| 栈依据 | 消抖逻辑 ~1 KB + 余量 |

### 4.5 ui_task

| 项 | 说明 |
|---|---|
| 职责 | 每 200 ms 读取 `room_state`（持锁），按优先级更新 RGB LED 颜色/亮度，可选更新 OLED |
| 输入 | `room_state_t`（通过 mutex 读取） |
| 输出 | GPIO 8 RMT/PWM 信号，I2C OLED（可选） |
| 周期 | 200 ms |
| 阻塞点 | `room_state_mutex` 持锁上限 5 ms；I2C OLED 写入超时 500 ms |
| 禁止事项 | 不得修改 `room_state`；不得执行网络操作 |
| 栈依据 | RGB 驱动 ~1 KB + OLED I2C ~1 KB + 余量 |

### 4.6 network_task

| 项 | 说明 |
|---|---|
| 职责 | 管理 Wi-Fi 连接状态，处理断开/重连/退避，发送网络状态事件到 `app_event_queue`（由 `state_machine_task` 消费以更新 `room_state.wifi_connected`） |
| 输入 | Wi-Fi 事件（ESP-IDF 事件循环） |
| 输出 | `app_event_t`（类型 `EVENT_NETWORK_STATUS`）到 `app_event_queue` |
| 周期 | 事件驱动 |
| 阻塞点 | 事件循环阻塞读（无超时） |
| 禁止事项 | 不得直接操作传感器；不得直接修改 Matter 属性；不得直接写入 `room_state` |
| 栈依据 | Wi-Fi 协议栈 ~4 KB + 事件处理 ~2 KB + 余量 |

### 4.7 matter_adapter_task

| 项 | 说明 |
|---|---|
| 职责 | 作为 ESP-Matter 内部 CHIP 任务的事件适配器：(1) 生命周期回调只做校验和非阻塞入队；ChangeToMode 使用有界请求/应答交给状态机，拿到真实转换结果后再返回命令状态；(2) 消费 `matter_report_queue`，在 ESP-Matter 要求的 CHIP stack 上下文/锁保护下更新属性；(3) 处理重连后的强制同步 |
| 输入 | Matter 事件回调；`matter_report_queue` 中的本地状态变更通知 |
| 输出 | Matter 属性更新（`occupancy`、`CurrentMode`）；`app_event_t`（类型 `EVENT_MATTER_*`）到 `app_event_queue` |
| 周期 | 事件驱动（Matter 回调 + 队列消费） |
| 阻塞点 | 普通回调禁止阻塞；ChangeToMode 最多等待状态机应答 100 ms；适配任务队列等待超时 2 s，用于健康检查和 TWDT 喂狗 |
| 禁止事项 | 不得执行传感器操作；不得直接操作 RGB LED；不得在回调中写 NVS、等待无界队列或直接修改 `room_state`；不得运行独立的 Matter event loop |
| 栈依据 | Matter 协议栈 ~8 KB + 属性处理 ~2 KB + 余量 |

> **重要**：ESP-Matter 已有内部 CHIP 任务。`matter_adapter_task` 不是再运行一套 Matter event loop，而是作为事件适配器，将 Matter 回调与本地事件系统桥接。

### 4.8 config_task

| 项 | 说明 |
|---|---|
| 职责 | 接收配置变更事件，执行 NVS 读写，处理配置版本迁移 |
| 输入 | `config_event_queue` |
| 输出 | NVS 持久化；配置变更通知 |
| 周期 | 事件驱动 |
| 阻塞点 | NVS 写入超时 5 s |
| 禁止事项 | 不得在 ISR 或高优先级任务中触发 NVS 写入 |
| 栈依据 | NVS 操作 ~2 KB + 迁移逻辑 ~1 KB + 余量 |

## 5. 队列与事件流

### 5.1 统一事件队列

| 队列名 | 元素类型 | 队列深度 | 发送方 | 接收方 | 说明 |
|---|---|---|---|---|---|
| `app_event_queue` | `app_event_t` | 32 | 所有传感器任务、按键任务、网络任务、Matter 回调适配器 | `state_machine_task` | 统一输入事件队列，只有状态机消费 |
| `gpio_evt_queue` | `uint32_t` (GPIO num) | 4 | Button ISR | `button_task` | 原始 GPIO 中断事件 |
| `config_event_queue` | `config_event_t` | 4 | `state_machine_task`, `matter_adapter_task` | `config_task` | 配置变更请求 |
| `matter_report_queue` | `matter_report_t` | 8 | `state_machine_task` | `matter_adapter_task` | 仅承载 occupancy/CurrentMode 变更和重连强制同步，不承载传感器原始数据 |

### 5.2 事件类型定义

```c
typedef enum {
    EVENT_RADAR_DATA,        // 雷达占用数据
    EVENT_ENV_DATA,          // 环境传感器数据
    EVENT_BUTTON,            // 按键事件（消抖后）
    EVENT_NETWORK_STATUS,    // 网络状态变化
    EVENT_MATTER_COMMAND,    // Matter 命令（如 ChangeToMode）
    EVENT_MATTER_READ,       // Matter 属性读取请求
    EVENT_CONFIG_CHANGE,     // 配置变更
    EVENT_TIMER_1S,          // 1 秒定时器超时
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        radar_data_t radar;
        env_data_t env;
        button_event_t button;
        network_status_t network;
        matter_command_t matter_cmd;
    } data;
    uint32_t timestamp_ms;
} app_event_t;
```

### 5.3 队列满策略

- `xQueueSend(..., 0)` 队列满时不会自动丢弃旧元素，禁止把发送失败误写成“已丢弃最旧数据”。
- 雷达和环境采样事件允许丢弃本次新样本，但必须递增分类丢弃计数；后续有效样本仍可恢复状态。
- 按键、网络状态和 Matter 命令不得静默丢弃。普通任务上下文最多等待 20 ms；ISR 和普通 Matter 生命周期回调只允许非阻塞发送。ChangeToMode 入队失败立即返回 Busy/Failure，入队后等待状态机应答上限 100 ms；超时不得返回成功。
- `state_machine_task` 是 `app_event_queue` 唯一消费者；任何生产者不得通过 `xQueueReceive` 自行“删最旧事件”。

### 5.4 ISR 规则

- 按键 ISR 只做：清除中断标志 → `xQueueSendFromISR(gpio_evt_queue, ...)` → `portYIELD_FROM_ISR`。
- 禁止在 ISR 中：执行消抖逻辑、访问 I2C/UART、分配内存、调用 `printf`、获取 mutex。

## 6. 共享资源与同步

### 6.1 共享资源表

| 资源 | 类型 | 所有者 (写入) | 读者 (读取) | 保护机制 | 持锁上限 |
|---|---|---|---|---|---|
| `room_state` | `room_state_t` (全局) | `state_machine_task` | `ui_task`, `matter_adapter_task` | `room_state_mutex` (FreeRTOS Mutex) | 10 ms |
| `device_config` | `config_t` (全局) | `config_task` | 所有任务 | `config_mutex` (FreeRTOS Mutex) | 5 ms |
| DHT22 单总线 | 硬件资源 | `sensor_env_task` | — | 单任务独占 + RMT channel | — |
| I2C 总线 | 硬件资源 | `ui_task` (OLED), 后续 SCD40 任务 | — | `i2c_mutex` (仅启用多个 I2C 使用者时创建) | 100 ms |
| UART1 | 硬件资源 | `sensor_radar_task` | — | 专用（单任务独占） | — |
| NVS | 存储 | `config_task` | — | 专用（单任务独占） | — |
| `app_event_queue` | FreeRTOS Queue | 所有任务 | `state_machine_task` | FreeRTOS 队列内置同步 | — |

> **重要**：`network_task` 不再直接写入 `room_state.wifi_connected`。网络状态变化通过 `app_event_queue` 发送 `EVENT_NETWORK_STATUS` 事件，由 `state_machine_task` 统一消费并更新 `room_state`。

### 6.2 死锁防护

- `room_state_mutex` 和 `config_mutex` 不会同时被持有。
- 如果 `state_machine_task` 需要同时读取配置和写入状态：先获取 `config_mutex` 读取配置副本 → 释放 `config_mutex` → 获取 `room_state_mutex` 写入状态 → 释放 `room_state_mutex`。
- DHT22 不使用 I2C mutex。首版只有 OLED 使用 I2C 时由 `ui_task` 独占；后续启用 SCD40 后，两个 I2C 使用者均以 100 ms 有界超时获取 `i2c_mutex`，不得持锁等待其他 mutex。

## 7. Watchdog 策略

### 7.1 Task Watchdog Timer (TWDT)

- 应用创建的 8 个任务在主循环稳定后注册到 TWDT；ESP-Matter/CHIP、Wi-Fi event loop 和 IDLE 等框架内部任务不由应用重复注册或手工喂狗。
- 每个已注册任务使用有界队列等待或周期唤醒，并在完成一轮健康工作后调用 `esp_task_wdt_reset()`。
- TWDT 超时：10 s。DHT22 5 s 周期和调度抖动下仍保留余量，不使用刚好等于任务周期的 5 s 超时。
- 超时触发：打印任务列表 → 系统重启。

### 7.2 各任务喂狗点

| 任务 | 喂狗位置 | 最大无喂狗间隔 |
|---|---|---|
| `sensor_radar_task` | 每轮 UART 读取完成后 | 500 ms |
| `sensor_env_task` | 每轮 DHT22 成功或有界失败处理后 | 5 s |
| `state_machine_task` | 每轮队列接收后（含超时） | 1 s |
| `button_task` | 按键处理完成或 2 s 队列等待超时后 | 2 s |
| `ui_task` | 每轮 RGB 更新后 | 200 ms |
| `network_task` | Wi-Fi 事件处理完成或 2 s 等待超时后 | 2 s |
| `matter_adapter_task` | 报告处理完成或 2 s 等待超时后 | 2 s |
| `config_task` | NVS 操作完成或 2 s 等待超时后 | 2 s |

> 事件驱动任务禁止使用 `portMAX_DELAY` 后又注册 TWDT。统一采用 2 s 有界等待；只有完整主循环仍可证明健康时才喂狗，不能在可能死锁的内部循环无条件喂狗。

## 8. 内存预算

### 8.1 ESP32-C6-WROOM-1 资源

| 资源 | 总量 | 预算 | 余量目标 |
|---|---|---|---|
| SRAM (HP) | 512 KB | 见下表 | ≥ 20% 空闲 |
| SRAM (LP) | 16 KB | 保留 | — |
| Flash | 16 MB（2026-07-17 已由 `flash_id` 实机确认） | 见下表 | 双 OTA 分区后仍保留诊断/存储余量 |

### 8.2 SRAM 预算（估算）

| 组件 | 预估 RAM | 说明 |
|---|---|---|
| FreeRTOS 任务栈总和 | ~42 KB | 8 个任务栈之和 |
| FreeRTOS 堆 | ~100 KB | 动态分配（Matter 协议栈、Wi-Fi 缓冲区等） |
| 静态全局变量 | ~8 KB | `room_state_t`, `config_t`, 队列缓冲区 |
| Wi-Fi/BLE 协议栈 | ~80 KB | ESP-IDF 内部分配 |
| Matter 协议栈 | ~60 KB | esp-matter 组件 |
| **合计** | ~290 KB | 余量 ~222 KB (43%) |

### 8.3 Flash 预算（估算）

| 组件 | 预估 Flash | 说明 |
|---|---|---|
| Bootloader + 分区表 | ~64 KB | ESP-IDF 默认 |
| 应用固件实际占用 | 预计 2–3 MB | 含 Matter 协议栈；以首次构建报告为准 |
| NVS | ~24 KB | 配置存储 |
| OTA 分区槽位 | 5 MB × 2 | A/B 各一个等大槽位，最大镜像不得超过槽位 |
| 诊断/存储余量 | 约 5 MB | 供 coredump、NVS 和未来本地数据使用；由 `partitions.csv` 最终冻结 |
| **总 Flash** | 16 MB | 已用 `esptool.py flash_id` 验证；最终仍以 `partitions.csv` 和构建报告校验边界 |

### 8.4 动态内存规则

- 优先使用静态分配（全局变量、静态缓冲区）。
- 动态分配必须设置上限：Matter 堆上限 120 KB，超出则记录告警。
- 不在 ISR 或高优先级任务中调用 `malloc`。
- 所有动态分配的对象在 `config_task` 或 `matter_task` 初始化时预分配。

## 9. 版本与变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 0.1 | 2026-07-11 | 初版：8 任务定义、数据流图、队列/事件流、共享资源、watchdog、内存预算 |
| 0.2 | 2026-07-16 | 按 LD2410C 连续上报协议修正雷达任务，并将 Flash 预算调整为待验证的 N16 16 MB |
| 0.3 | 2026-07-17 | 环境任务改为 DHT22/RMT；修正统一队列、Matter 上报队列、I2C 所有权和 TWDT 语义；确认 16 MB Flash |
