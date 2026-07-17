# Matter 数据模型

## 1. 设计原则

本数据模型遵循以下原则：

1. **占用、模式与本地告警分离**：`occupancy`（是否有人）和 `user mode`（安静/夜间）映射到独立 Endpoint；`environment alert` 首版只在本地评估，不伪造通用告警 Endpoint。
2. **标准优先**：首版只使用 Matter 标准设备类型和 Cluster，不定义自定义 Cluster，确保与主流控制器互操作。
3. **本地优先**：所有状态由本地状态机计算，Matter 属性是本地状态的投影。断网时本地状态继续运行，重连后同步。
4. **最小映射**：只映射控制器可读取和自动化可使用的属性，不上传原始雷达数据或传感器原始值。

## 2. Endpoint 拓扑

```text
Endpoint 0: Root Node（标准，必需）
└─ Root/commissioning/access/diagnostic Cluster 集合由锁定的 ESP-Matter
   版本和生成配置产生；文档不手抄一份可能随 SDK 变化的完整列表。

Endpoint 1: Occupancy Sensor（设备类型 0x0107）
└─ Cluster: OccupancySensing (0x0406)
   ├─ occupancy          — 占用位图，bit0 = occupied
   ├─ occupancySensorType — 0x00 (PIR)，标准中最接近雷达的类型
   └─ occupancySensorTypeBitmap — bit0 = PIR

Endpoint 2: Mode Select（设备类型 0x0027）
└─ Cluster: ModeSelect (0x0050)
   ├─ Description         = 0x0000
   ├─ StandardNamespace   = 0x0001
   ├─ SupportedModes     = 0x0002
   └─ CurrentMode        = 0x0003

本地环境告警（不属于 Matter Endpoint）
└─ AM2302/DHT22 → env_alert → RGB/OLED
   后续如需要互操作，再增加标准 Temperature Sensor / Humidity Sensor Endpoint。
```

## 3. Endpoint 1：Occupancy Sensor

### 3.1 设备类型

| 字段 | 值 |
|---|---|
| Device Type ID | `0x0107` |
| Device Type Name | `MA-occupancy-sensor` |
| Domain | `MA` |

### 3.2 Cluster: OccupancySensing (0x0406)

| 属性 ID | 属性名称 | 类型 | 值域 | 可写 | 默认值 |
|---|---|---|---|---|---|
| 0x0000 | `occupancy` | bitmap8 | bit0: 0=unoccupied, 1=occupied | 否 | 0 |
| 0x0001 | `occupancySensorType` | enum8 | 0x00=PIR, 0x01=Ultrasonic, 0x02=PhysicalContact | 否 | 0x00 (PIR) |
| 0x0002 | `occupancySensorTypeBitmap` | bitmap8 | bit0=PIR, bit1=Ultrasonic, bit2=PhysicalContact | 否 | 0x01 |

> **说明**：Matter 标准未定义"毫米波雷达"传感器类型。首版使用 `PIR (0x00)` 作为最接近的标准值。这不会影响功能正确性——`occupancy` 属性的语义是"检测到有人"，与传感器物理原理无关。

> 这是作品原型的互操作折中，不等同于毫米波已被标准定义为 PIR。若进入认证或严格产品化，必须以锁定 Matter 版本的设备类型测试和目标生态兼容结果重新评审该枚举值。

### 3.3 数据来源

| 属性 | 数据来源 | 本地变量 | 更新触发 |
|---|---|---|---|
| `occupancy` | LD2410C 雷达 → 本地占用状态机 → `room_state.occupancy` | `occupancy_state_t` (VACANT / OCCUPIED / UNKNOWN) | 占用状态转换时（经去抖确认），非每次雷达轮询 |
| `occupancySensorType` | 固定值 | N/A | 固化，不更新 |
| `occupancySensorTypeBitmap` | 固定值 | N/A | 固化，不更新 |

### 3.4 更新条件

- `occupancy` 属性仅在本地占用状态机发生 **VACANT → OCCUPIED** 或 **OCCUPIED → VACANT** 转换时更新。
- 转换已包含去抖延迟（进入确认 `ENTRY_CONFIRM_MS`，退出延迟 `EXIT_DELAY_MS`），不会因单次采样抖动触发更新。
- `UNKNOWN`（传感器失效）状态不下发到 Matter；保留上一次有效值，由控制器侧的超时机制处理。详见 [状态模型](state-model.md) 第 4 节。

### 3.5 离线行为

| 场景 | 本地行为 | Matter 属性行为 |
|---|---|---|
| Wi-Fi 断开 | 占用状态机继续运行，RGB 正常显示 | 无法发送报告；控制器只能通过会话/Subscription 中断或读取失败判断离线，不假设存在 `LastOperationalEpoch` 属性 |
| Wi-Fi 重连 | 状态机继续运行 | 重连后立即上报当前 `occupancy` 值（force update） |
| Matter 会话失败 | 状态机继续运行 | 属性不更新，Matter 层自行重试 |
| 雷达断线 | 占用状态 = UNKNOWN，RGB 显示故障 | `occupancy` 保留上一次有效值（不更新为 0），详见状态模型第 4 节 |
| 设备重启 | 从 NVS 恢复配置，状态机初始化为 VACANT | 首次连接后上报 VACANT（除非配置了恢复策略） |

## 4. Endpoint 2：Mode Select

### 4.1 设备类型

| 字段 | 值 |
|---|---|
| Device Type ID | `0x0027` |
| Device Type Name | `MA-mode-select` |
| Domain | `MA` |

### 4.2 Cluster: ModeSelect (0x0050)

| 属性 ID | 属性名称 | 类型 | 值域 | 可写 | 默认值 |
|---|---|---|---|---|---|
| 0x0000 | `Description` | char string | "Room Mode" | 否 | "Room Mode" |
| 0x0001 | `StandardNamespace` | nullable uint16 | 标准语义标签命名空间；首版不用 | 否 | null |
| 0x0002 | `SupportedModes` | array<ModeOptionStruct> | 见下表 | 否 | 预填充 |
| 0x0003 | `CurrentMode` | enum8 | 0=NORMAL, 1=QUIET, 2=NIGHT | 否 | 0 |

### 4.3 ModeOptionStruct 定义

| Mode | Label | ModeTags |
|---|---|---|
| 0 | "Normal" | [] |
| 1 | "Quiet" | [] |
| 2 | "Night" | [] |

首版不声明未经验证的标准语义标签，因此 `StandardNamespace = null`、各模式 `ModeTags = []`。ESP-Matter 的 SupportedModes manager/factory data 必须与此表一致。

### 4.4 模式切换命令

- `CurrentMode` 不是直接可写属性。
- 控制器应调用 `ChangeToMode` 命令（命令 ID 0x00）切换模式，参数为 `newMode`（uint8）。
- 设备收到命令后先验证 `newMode` 是否在 `SupportedModes` 中，再由本地状态机判断当前转换是否允许；只有状态真正切换后才返回成功。
- 本地用户按键切换模式时，直接更新 `CurrentMode` 并触发属性上报。

### 4.5 数据来源

| 属性 | 数据来源 | 本地变量 | 更新触发 |
|---|---|---|---|
| `CurrentMode` | 用户按键 + 时段判定 → 本地模式状态机 → `room_state.user_mode` | `user_mode_t` (NORMAL / QUIET / NIGHT) | 模式转换时；或控制器调用 ChangeToMode 命令时 |
| `SupportedModes` | 固定值 | N/A | 固化 |
| `Description` | 固定值 | N/A | 固化 |
| `StandardNamespace` | 固定值 | N/A | 固化 |

### 4.6 控制器写入行为

- 控制器通过 `ChangeToMode` 命令切换模式，等价于用户按键切换模式。
- 请求 `NIGHT` 时，仅在本地时间有效且处于配置夜间窗口时允许；不满足条件必须返回命令失败，不能保持原状态却返回成功。
- 写入 `QUIET` 时立即生效（用户意图优先）。
- 写入 `NORMAL` 时清除 QUIET 标志。

### 4.7 更新条件

- `CurrentMode` 在以下情况更新：
  1. 用户短按按键 → 切换 QUIET 开/关。
  2. 进入/退出配置的夜间时段 → 切换 NIGHT。
  3. 控制器通过 `ChangeToMode` 命令切换。

### 4.8 离线行为

| 场景 | 本地行为 | Matter 属性行为 |
|---|---|---|
| Wi-Fi 断开 | 模式可正常切换（按键 + 时段） | 属性不更新 |
| Wi-Fi 重连 | 模式继续运行 | 重连后立即上报当前 `CurrentMode` |
| 设备重启 | 从 NVS 恢复 QUIET 状态；NIGHT 由当前时间重新判定 | 首次连接后上报当前模式 |

## 5. 本地环境告警（首版无 Matter Endpoint）

> **重要**：首版锁定的 Matter 数据模型中没有与“温湿度综合告警”语义完全匹配的标准设备类型。即使 SDK 提供 Boolean State/On-Off Sensor 能力，也不应为了凑 Endpoint 而把环境告警伪装成门锁、接触或普通开关状态。
> 首版环境告警仅在本地状态机和 RGB/OLED 中评估，不上报为独立 Matter Endpoint。
> 
> 后续可选方案：
> - **方案 A**：将温湿度数据映射为标准 Temperature Sensor (0x0302) / Humidity Sensor (0x0307) Endpoint
> - **方案 B**：使用自定义 Cluster（牺牲与第三方控制器的互操作性）
> - **方案 C**：首版只本地显示，不映射到 Matter

### 5.1 本地告警评估（不上报 Matter）

环境告警在本地状态机中评估，仅用于 RGB 显示和本地逻辑：

| 本地变量 | 类型 | 说明 |
|---|---|---|
| `env_alert` | `env_alert_t` | OK / ALERT |
| 评估指标 | 温度、湿度、CO2 (可选) | 来自 AM2302/DHT22；SCD40 仅为后续扩展 |

### 5.2 告警阈值（首版默认值，可通过 NVS 配置）

| 指标 | 告警阈值 (ALERT) | 清除阈值 (CLEAR) | 确认时间 | 清除时间 |
|---|---|---|---|---|
| 温度 | > 32 °C | < 30 °C | 60 s | 120 s |
| 湿度 | > 75 %RH | < 70 %RH | 60 s | 120 s |
| CO2 (可选) | > 1000 ppm | < 800 ppm | 60 s | 120 s |

- 任一指标超过告警阈值并持续确认时间 → `env_alert = ALERT`。
- 全部指标低于清除阈值并持续清除时间 → `env_alert = OK`。
- 滞回设计避免边界抖动。

### 5.3 更新条件

- `env_alert` 仅在本地告警状态机发生 **OK → ALERT** 或 **ALERT → OK** 转换时更新。
- 环境告警仅在占用状态为 `OCCUPIED` 时有意义。当 `OCCUPIED → VACANT` 时，如果当前告警为 ALERT，自动清除为 OK（无人时不需要通风提醒）。

### 5.4 离线行为

| 场景 | 本地行为 |
|---|---|
| Wi-Fi 断开 | 告警评估继续运行 |
| Wi-Fi 重连 | 告警评估继续运行 |
| DHT22 无响应、超时或 checksum 连续失败 | 告警冻结为上一次状态，RGB/OLED 显示传感器故障 |
| 设备重启 | 告警初始化为 OK |

## 6. 属性上报策略

### 6.1 上报方式

首版使用 **属性变更通知 + 控制器读取** 模式：

1. 本地状态变化时，调用 Matter 层标记属性为 dirty。
2. Matter 层在下次会话活跃时通过 `ReportData` 消息通知控制器。
3. 控制器也可主动读取属性值。

> **不使用** Matter Subscription 机制的首版强制配置，但如果控制器主动建立 Subscription，设备应支持。

### 6.2 上报频率限制

| 属性 | 最小上报间隔 | 原因 |
|---|---|---|
| `occupancy` | 1 s | 防止快速进出导致的频繁上报 |
| `CurrentMode` | 无限制 | 用户操作频率低 |

### 6.3 重连后同步

Wi-Fi 重连且 Matter 会话恢复后，按以下顺序强制上报：
1. `occupancy`（Endpoint 1）
2. `CurrentMode`（Endpoint 2）

环境告警首版没有 Matter 属性，不参与重连强制上报。

## 7. 与本地状态模型的关系

Matter 属性是本地状态模型的投影；`CurrentMode` 由控制器通过 `ChangeToMode` 命令请求变更，而不是直接写属性。完整的状态转换逻辑、阈值、传感器失效策略见 [状态模型](state-model.md)。

```text
LD2410C ──→ 占用状态机 ──→ room_state.occupancy ──→ Endpoint 1: occupancy
                                        │
按键 ──→ 模式状态机 ──→ room_state.user_mode ──→ Endpoint 2: CurrentMode
时段 ──→               │
                        │
DHT22/SCD40 ──→ 告警状态机 ──→ room_state.env_alert ──→ 本地 RGB/OLED（首版不上报 Matter）
```

三个维度完全独立计算，不互相覆盖。RGB 显示逻辑根据优先级组合显示三者，详见状态模型第 6 节。

## 8. 版本与变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 0.1 | 2026-07-11 | 初版：定义 4 个 Endpoint、3 个业务 Cluster、属性映射和离线行为 |
| 0.2 | 2026-07-17 | 首版统一为 Endpoint 0/1/2 和 2 个业务 Cluster；移除虚假的 Endpoint 3/stateValue；修正 ModeSelect 类型、标签和失败语义；环境源改为 DHT22 |

## 9. 实现依据

- [Matter 标准设备类型列表](https://project-chip.github.io/connectedhomeip-doc/ids_and_codes/spec_device_types.html)：Mode Select `0x0027`、Occupancy Sensor `0x0107`。
- [ESP-Matter Mode Select 指南](https://docs.espressif.com/projects/esp-matter/en/release-v1.4.2/esp32c6/developing.html#mode-select)：SupportedModes manager 和 factory data 用法。实现时以本机锁定的 `release/v1.5` 源码/API 为最终依据。
