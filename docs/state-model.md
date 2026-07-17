# 状态模型

## 1. 设计原则：并行模型

原设计将 `vacant`、`active`、`quiet`、`night`、`environment_alert` 放在单一状态机中，导致 `environment_alert` 覆盖 `vacant`/`active` 时占用信息丢失。本模型拆分为 **三个并行维度**，各自独立计算和存储，不互相覆盖：

```text
维度 1: Occupancy（占用）     → VACANT / OCCUPIED / UNKNOWN
维度 2: User Mode（用户模式）  → NORMAL / QUIET / NIGHT
维度 3: Env Alert（环境告警）  → OK / ALERT
```

组合示例：`OCCUPIED + QUIET + OK` = 有人在房间、安静模式、环境正常。

Matter 属性映射见 [Matter 数据模型](matter-data-model.md)：占用和用户模式分别对应 Endpoint 1/2；环境告警首版仅保留在本地状态与 RGB/OLED，不创建 Endpoint 3。

## 2. 维度 1：占用状态 (Occupancy)

### 2.1 状态定义

| 状态 | 含义 | 本地变量 |
|---|---|---|
| `VACANT` | 无人 | `OCCUPANCY_VACANT` |
| `OCCUPIED` | 有人 | `OCCUPANCY_OCCUPIED` |
| `UNKNOWN` | 传感器失效，无法判定 | `OCCUPANCY_UNKNOWN` |

### 2.2 数据来源

- 传感器：LD2410C 毫米波雷达
- 接口：UART1 连续接收正常模式上报帧；工程模式仅用于标定和诊断
- 状态评估周期：200 ms（雷达数据本身为连续上报，不发送轮询命令）
- 原始数据：`moving_distance`、`static_distance`、`moving_energy`、`static_energy`、`target_state`

### 2.3 转换表

| 当前状态 | 事件 | 条件 | 目标状态 | 延迟/计时器 |
|---|---|---|---|---|
| `VACANT` | 雷达报告有人 | 连续 ≥ `ENTRY_CONFIRM_MS` (默认 2000 ms) | `OCCUPIED` | 进入确认计时器 |
| `OCCUPIED` | 雷达报告无人 | 连续 ≥ `EXIT_DELAY_MS` (默认 120000 ms) | `VACANT` | 退出延迟计时器 |
| `VACANT` / `OCCUPIED` | 雷达通信失败 | 连续 ≥ `SENSOR_TIMEOUT_MS` (默认 10000 ms) | `UNKNOWN` | 传感器超时计时器 |
| `UNKNOWN` | 雷达通信恢复 | 成功读取 1 帧 | 根据恢复帧重新执行进入/退出确认 | 确认前保持 `UNKNOWN`，确认期间启动 `ENTRY_CONFIRM_MS` 或 `EXIT_DELAY_MS` 计时器 |

### 2.4 参数定义

| 参数 | 默认值 | 范围 | 说明 |
|---|---|---|---|
| `ENTRY_CONFIRM_MS` | 2000 | 500–5000 | 进入确认：防止短时经过误触发 |
| `EXIT_DELAY_MS` | 120000 | 30000–600000 | 退出延迟：防止短暂遮挡或静止误判无人 |
| `SENSOR_TIMEOUT_MS` | 10000 | 5000–30000 | 传感器超时：连续无有效数据判定为断线 |
| `RADAR_EVAL_MS` | 200 | 100–500 | 占用状态评估周期，不是 UART 命令轮询周期 |

所有参数存储在 NVS，可通过配置接口修改。`ENTRY_CONFIRM_MS` 和 `EXIT_DELAY_MS` 是雷达侧和本地侧双重去抖——LD2410C 自带 unoccupied_duration 参数，本地侧再做一层确认。

### 2.5 误触发防护

- 单次雷达采样不直接触发状态转换。
- `ENTRY_CONFIRM_MS` 内如果雷达报告无人，计时器清零，不转换。
- `EXIT_DELAY_MS` 内如果雷达报告有人，计时器清零，不转换。
- 雷达自身的灵敏度参数（gate 阈值）在阶段 1 硬件验证时标定，不在此文档定义。

## 3. 维度 2：用户模式 (User Mode)

### 3.1 状态定义

| 状态 | 含义 | 本地变量 |
|---|---|---|
| `NORMAL` | 默认模式 | `MODE_NORMAL` |
| `QUIET` | 用户主动切换的安静模式 | `MODE_QUIET` |
| `NIGHT` | 夜间时段自动切换 | `MODE_NIGHT` |

### 3.2 优先级

`NIGHT > QUIET > NORMAL`

- 夜间时段内，无论 QUIET 是否激活，模式为 `NIGHT`。
- 夜间时段结束，恢复进入 NIGHT 前的模式：
  - 如果之前是 `QUIET` → 恢复为 `QUIET`。
  - 如果之前是 `NORMAL` → 恢复为 `NORMAL`。

### 3.3 数据来源

- 按键：GPIO 9（DevKitC-1 Boot 按钮，active low），短按切换 QUIET。
- 时段：本地 RTC 时间（通过 SNTP 同步，断网时使用上次同步时间 + 上电运行时间估算）。
- 夜间时段配置：默认 22:00–07:00，存储在 NVS。

> **night 判定方式说明**：用户已有 LM393 光敏电阻模块，但它只能输出阈值明暗或未标定模拟值。为缩短首版开发周期，自动 `NIGHT` 固定为 **SNTP 时段判定 + NVS 配置**；光敏模块在用户明确扩大范围后再接入 GPIO10。

### 3.4 转换表

| 当前状态 | 事件 | 条件 | 目标状态 | 说明 |
|---|---|---|---|---|
| `NORMAL` | 短按按键 | 消抖确认（见 3.6） | `QUIET` | 用户主动进入安静 |
| `QUIET` | 短按按键 | 消抖确认 | `NORMAL` | 用户退出安静 |
| `NORMAL` / `QUIET` | 进入夜间时段 | 当前时间 ∈ [NIGHT_START, NIGHT_END) | `NIGHT` | 记录进入前模式 |
| `NIGHT` | 退出夜间时段 | 当前时间 ∉ [NIGHT_START, NIGHT_END) | 恢复进入前模式 | QUIET 或 NORMAL |
| 任意 | Matter 控制器请求 `QUIET` | ModeSelect `ChangeToMode(newMode=1)` 通过校验 | `QUIET` | 等效用户短按 |
| 任意 | Matter 控制器请求 `NORMAL` | ModeSelect `ChangeToMode(newMode=0)` 通过校验 | `NORMAL` | 清除 QUIET 标志 |

### 3.5 时段判定规则

- 夜间时段为 `[NIGHT_START, NIGHT_END)`，支持跨午夜（如 22:00–07:00）。
- 判定周期：每 60 s 检查一次。
- 时间来源优先级：
  1. SNTP 同步时间（网络可用时）。
  2. 上电运行时间 + 上次同步时间（网络不可用时，仅在同一次上电期间有效）。
  3. 如果从未同步过时间或设备已重启 → 时间标记为未知，不启用 NIGHT 模式，仅依赖按键。

> **重要**：ESP32-C6 没有后备实时时钟（RTC）。断电后无法保持时间。只能在同一次上电期间使用"上次同步时间 + 上电运行时间"估算。断电重启且 SNTP 未恢复时，应禁用自动 NIGHT 模式。

### 3.6 按键消抖与检测

| 参数 | 默认值 | 说明 |
|---|---|---|
| `BUTTON_DEBOUNCE_MS` | 50 | 软件消抖时间 |
| `BUTTON_SHORT_PRESS_MS` | 50–1000 | 短按判定窗口（按下 50 ms 后释放） |
| `BUTTON_LONG_PRESS_MS` | 5000 | 长按判定阈值（持续按下 ≥ 5 s） |
| `BUTTON_FACTORY_RESET_MS` | 5000 | 长按达到此值触发恢复出厂（见 [配网生命周期](commissioning-lifecycle.md)） |

- 短按：切换 QUIET 开/关。
- 长按 ≥ 5 s：触发恢复出厂/重新配网模式。
- 长按过程中 RGB 显示倒计时指示（从 5 s 开始每秒闪烁一次红色）。

## 4. 维度 3：环境告警 (Env Alert)

### 4.1 状态定义

| 状态 | 含义 | 本地变量 |
|---|---|---|
| `OK` | 所有指标在正常范围内 | `ALERT_OK` |
| `ALERT` | 至少一个指标持续超阈值 | `ALERT_ACTIVE` |

### 4.2 数据来源

| 传感器 | 指标 | 接口 | 采样周期 |
|---|---|---|---|
| AM2302/DHT22 | 温度 (°C) | GPIO2 单总线（RMT 捕获） | 5 s（不得短于 2 s） |
| AM2302/DHT22 | 湿度 (%RH) | GPIO2 单总线（RMT 捕获） | 5 s（不得短于 2 s） |
| SCD40 (可选) | CO2 (ppm) | I2C | 5 s（传感器内部 5 s 周期） |

### 4.3 阈值与滞回

| 指标 | 告警阈值 | 清除阈值 | 确认时间 | 清除时间 |
|---|---|---|---|---|
| 温度 | > 32 °C | < 30 °C | 60 s | 120 s |
| 湿度 | > 75 %RH | < 70 %RH | 60 s | 120 s |
| CO2 (可选) | > 1000 ppm | < 800 ppm | 60 s | 120 s |

- 告警阈值和清除阈值之间的差值为滞回区间，防止边界抖动。
- 确认时间和清除时间防止短时波动误触发。

### 4.4 转换表

| 当前状态 | 事件 | 条件 | 目标状态 |
|---|---|---|---|
| `OK` | 任一指标 > 告警阈值 | 持续 ≥ 确认时间 (60 s) | `ALERT` |
| `ALERT` | 全部指标 < 清除阈值 | 持续 ≥ 清除时间 (120 s) | `OK` |
| `ALERT` | 占用状态变为 `VACANT` | 无条件 | `OK` |
| `OK` / `ALERT` | 传感器断线 | 见 5.2 传感器失效策略 | 冻结 |

### 4.5 与占用状态的交互

- 环境告警仅在 `OCCUPIED` 时有意义。
- `OCCUPIED → VACANT` 时自动清除告警为 `OK`（无人时不需要通风提醒）。
- `VACANT → OCCUPIED` 时重新开始评估（不立即告警，需经过确认时间）。
- `UNKNOWN` 时告警冻结为上一次状态。

## 5. 传感器失效策略

### 5.1 雷达 (LD2410C) 失效

| 场景 | 检测方式 | 占用状态 | RGB 显示 | Matter 属性 | 恢复行为 |
|---|---|---|---|---|---|
| UART 通信失败 | 连续 `SENSOR_TIMEOUT_MS` 无有效帧 | `UNKNOWN` | 红色慢闪 (1 Hz) | `occupancy` 保留上一次有效值 | 雷达恢复后 → 根据恢复帧重新执行进入/退出确认 |
| 雷达数据异常 | 帧头/长度/帧尾校验失败、字段值超出范围、或连续超时 | `UNKNOWN` | 红色慢闪 (1 Hz) | `occupancy` 保留上一次有效值 | 同上 |
| 雷达配置丢失 | 雷达返回默认参数 | 正常运行 | 正常 | 正常 | 记录日志，下次启动时重新配置 |

### 5.2 环境传感器 (AM2302/DHT22) 失效

| 场景 | 检测方式 | 告警状态 | RGB 显示 | Matter 属性 | 恢复行为 |
|---|---|---|---|---|---|
| 无响应、脉宽超时或 checksum 失败 | 连续 3 次读取失败（5 s 周期，约 15 s） | 冻结为上一次状态 | 附加黄色短闪 | 首版无环境 Matter 属性 | 恢复且得到一帧有效数据后重新开始评估 |
| 数据超出合理范围 | 温度 < -40 或 > 80 °C，或湿度 < 0 或 > 100 %RH | 忽略该次数据 | 附加黄色短闪 | 首版无环境 Matter 属性 | 连续 5 次异常 → 判定传感器失效 |

### 5.3 多传感器失效叠加

- 雷达 + 环境传感器同时失效时，RGB 以最高优先级显示（见第 6 节）。
- Matter `occupancy` 保留上一次有效值；环境告警首版没有 Matter 属性。
- 设备不因传感器失效而重启，保持本地可用性。

## 6. RGB 显示优先级

RGB 状态灯按优先级从高到低显示，同一时刻只显示最高优先级的状态：

| 优先级 | 条件 | RGB 表现 |
|---|---|---|
| 1 (最高) | 任何传感器失效 | 红色慢闪 (1 Hz)，每 10 s 附加一次黄色短闪表示环境传感器也失效 |
| 2 | 配网模式 | 蓝色快闪 (2 Hz) |
| 3 | Wi-Fi 断开 | 白色慢闪 (0.5 Hz) |
| 4 | 环境告警 `ALERT` | 黄色常亮（亮度 50%） |
| 5 | 占用 `OCCUPIED` + 模式 `NIGHT` | 暖白色低亮（亮度 10%） |
| 6 | 占用 `OCCUPIED` + 模式 `QUIET` | 蓝色低亮（亮度 20%） |
| 7 | 占用 `OCCUPIED` + 模式 `NORMAL` | 绿色常亮（亮度 50%） |
| 8 (最低) | 占用 `VACANT` | 熄灭 |

> 按键长按倒计时期间，RGB 覆盖为红色每秒闪烁，优先级高于上述所有状态。

## 7. 状态变量数据结构

```c
typedef enum {
    OCCUPANCY_VACANT = 0,
    OCCUPANCY_OCCUPIED,
    OCCUPANCY_UNKNOWN
} occupancy_state_t;

typedef enum {
    MODE_NORMAL = 0,
    MODE_QUIET,
    MODE_NIGHT
} user_mode_t;

typedef enum {
    ALERT_OK = 0,
    ALERT_ACTIVE
} env_alert_t;

typedef struct {
    occupancy_state_t occupancy;
    user_mode_t       user_mode;
    user_mode_t       pre_night_mode;   /* 进入 NIGHT 前的模式 */
    env_alert_t       env_alert;
    bool              quiet_active;     /* 用户是否激活了 QUIET */
    bool              wifi_connected;
    bool              matter_commissioned;
    bool              radar_online;
    bool              env_sensor_online;
} room_state_t;
```

- `room_state_t` 由 `state_machine_task` 拥有并写入。
- 其他任务通过 `room_state_mutex` 读取。
- 详见 [任务架构](task-architecture.md)。

## 8. 配置参数汇总

以下参数存储在项目独立的 NVS namespace。首版 Matter 只暴露 ModeSelect，不提供阈值和夜间时间配置 Cluster；因此阈值及 `NIGHT_START/NIGHT_END` 先使用默认值或受控开发配置入口，后续增加正式配置接口时再扩展数据模型：

| 参数 | 默认值 | 范围 | 说明 |
|---|---|---|---|
| `ENTRY_CONFIRM_MS` | 2000 | 500–5000 | 占用进入确认时间 |
| `EXIT_DELAY_MS` | 120000 | 30000–600000 | 占用退出延迟 |
| `SENSOR_TIMEOUT_MS` | 10000 | 5000–30000 | 雷达超时 |
| `NIGHT_START` | "22:00" | "00:00"–"23:59" | 夜间开始时间 |
| `NIGHT_END` | "07:00" | "00:00"–"23:59" | 夜间结束时间 |
| `TEMP_ALERT` | 32.0 | 25.0–45.0 | 温度告警阈值 (°C) |
| `TEMP_CLEAR` | 30.0 | 20.0–40.0 | 温度清除阈值 (°C) |
| `HUMID_ALERT` | 75.0 | 60.0–95.0 | 湿度告警阈值 (%RH) |
| `HUMID_CLEAR` | 70.0 | 50.0–90.0 | 湿度清除阈值 (%RH) |
| `CO2_ALERT` | 1000 | 600–2000 | CO2 告警阈值 (ppm) |
| `CO2_CLEAR` | 800 | 400–1500 | CO2 清除阈值 (ppm) |
| `ALERT_CONFIRM_S` | 60 | 10–300 | 告警确认时间 (s) |
| `ALERT_CLEAR_S` | 120 | 30–600 | 告警清除时间 (s) |
| `CONFIG_VERSION` | 1 | 1–65535 | 配置版本号（用于迁移） |

## 9. 版本与变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 0.1 | 2026-07-11 | 初版：并行三维度模型、转换表、阈值、传感器失效策略、RGB 优先级 |
| 0.2 | 2026-07-17 | 温湿度源改为 DHT22；环境告警固定为本地状态；NIGHT 保持时段判定；修正 Matter 控制和失效语义 |
