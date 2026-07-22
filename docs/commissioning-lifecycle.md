# 配网与断网恢复生命周期

## 1. 概述

本文档定义设备从首次上电到正常运行全过程中的配网、断网恢复、恢复出厂和配置迁移规则。目标是确保设备在各种异常场景下保留本地可用性，并防止凭据丢失或配置损坏导致设备变砖。

## 2. 设备启动流程

```text
上电
  │
  ▼
读取 NVS 配置 ──失败──→ 加载默认配置，标记 config_corrupted=true
  │ (成功)
  ▼
配置版本迁移检查 ──需要迁移──→ 执行迁移函数 ──失败──→ 回退默认配置，记录告警
  │ (无需迁移或迁移成功)
  ▼
初始化传感器、RGB、状态机
  │
  ▼
检查是否已配网 (NVS 中有 Wi-Fi 凭据 + Matter commissioned=true)
  │
  ├──否──→ 进入 BLE Commissioning 模式（第 3 节）
  │
  └──是──→ 尝试 Wi-Fi 连接（第 4 节）
              │
              ├──成功──→ 启动 Matter，进入正常运行
              │
              └──失败──→ 本地状态机正常运行，Wi-Fi 退避重连
```

## 3. BLE Commissioning 生命周期

### 3.1 进入条件

- 首次上电（NVS 无 Wi-Fi 凭据）。
- 恢复出厂后重启。
- 用户长按按键 ≥ 5 s 触发重新配网。

### 3.2 流程

| 步骤 | 操作 | 超时 | 重试 | 失败处理 |
|---|---|---|---|---|
| 1. 启动 BLE 广播 | ESP-Matter 初始化 BLE 广播，使用 Factory/Custom Commissionable Data Provider 提供的 setup passcode、discriminator 和 SPAKE2+ verifier | — | — | BLE 初始化失败 → RGB 红色常亮，等待 30 s 后重启 |
| 2. 等待控制器连接 | BLE 广播等待连接 | 5 min | 到超时后停止广播，RGB 白色慢闪，等待按键重新触发 | — |
| 3. Matter commissioning | 执行 Matter PAKE 交换和设备配网（SPAKE2+ 验证使用 Commissionable Data Provider 提供的 verifier） | 60 s | 最多 3 次，退避 5 s / 10 s / 20 s | 超过重试上限 → 停止 BLE，RGB 红色快闪，等待按键重新触发 |
| 4. 接收 Wi-Fi 凭据 | 通过 Matter Network Commissioning Cluster 接收 SSID 和密码 | 包含在步骤 3 中 | — | — |
| 5. 写入 NVS | 保存 Wi-Fi 凭据和 Matter commissioning 数据 | 5 s | 最多 3 次 | 写入失败 → 不清除已有凭据（如果有），保留 BLE 活跃，RGB 红色常亮，记录错误 |
| 6. 停止 BLE | ESP-Matter 反初始化 BLE 栈，释放 BLE 缓冲区 | — | — | — |
| 7. 连接 Wi-Fi | 使用新凭据连接 Wi-Fi | 15 s | 最多 3 次 | 连接失败 → 进入 Wi-Fi 退避重连（第 4 节） |

> **重要**：
> - Setup passcode、discriminator、SPAKE2+ verifier 来自 Test/Factory/Custom Commissionable Data Provider，不是"首次启动随机生成并通过 BLE 发送"。
> - 首版可使用 Test Commissionable Data Provider（固定 passcode/discriminator），或使用 Factory Data Provider（从 NVS/Flash 读取预烧录的 factory data）。
> - 详见 ESP-Matter Factory Data Providers 文档。

### 3.3 凭据写入失败处理

- NVS 写入失败时**不清除**已有有效凭据。
- 如果是首次配网（无已有凭据），则保持 BLE 活跃，允许控制器重新尝试。
- 如果是非首次（已有旧凭据），保留旧凭据，尝试用旧凭据连接 Wi-Fi，同时记录写入失败。
- 连续 3 次 NVS 写入失败 → 标记 `nvs_critical_error`，RGB 红色常亮，等待用户长按重启。

### 3.4 BLE 资源释放

BLE commissioning 完成后（成功或超时放弃后），必须释放以下资源：

| 资源 | 释放操作 | 说明 |
|---|---|---|
| BLE 广播 | ESP-Matter 内部接口 | 停止广播 |
| BLE GATT 服务 | ESP-Matter 内部接口 | 注销 GATT 服务 |
| BLE 控制器 | ESP-Matter 内部接口 | 禁用并反初始化控制器 |
| BLE 内存 | `esp_bt_mem_release(ESP_BT_MODE_BLE)` | 释放 BLE 专用内存 |

> **重要**：
> - 项目使用 **NimBLE** 栈，不是 Bluedroid。不得使用 `esp_bluedroid_*` 或 `esp_bt_gap_*` 等 Bluedroid API。
> - BLE 释放应通过 ESP-Matter 提供的接口完成，不应由业务代码直接操作 BLE 栈。
> - BLE 仅用于 commissioning，正常运行期间不保持 BLE 活跃。释放的内存（约 20–30 KB）归还给系统堆，供 Matter 和 Wi-Fi 使用。

### 3.5 重新配网触发

- **用户触发**：长按按键 ≥ 5 s（RGB 红色倒计时闪烁），松开后调用 ESP-Matter 提供的重置接口清除 Wi-Fi 凭据和 Matter commissioning 数据，重启进入 BLE Commissioning 模式。
- **Matter 控制器触发**：通过 General Commissioning Cluster 的 `ArmFailSafe` 命令（首版可选支持）。注意：`ArmFailSafe` 不是恢复出厂或重新配网命令，它只是激活故障安全计时器，超时后设备会回退到未配网状态。
- **防误触**：长按需持续 5 s，期间 RGB 每秒闪烁一次红色作为倒计时提示。如果在 5 s 内释放，取消操作。

## 4. Wi-Fi 连接与断网恢复

### 4.1 首次连接

- 使用 commissioning 阶段获取的 SSID 和密码。
- 超时 15 s，最多重试 3 次。
- 3 次失败后进入退避重连模式。

### 4.2 断网重连策略

| 参数 | 值 | 说明 |
|---|---|---|
| 初始退避 | 1 s | 第一次重连等待 |
| 退避倍增 | ×2 | 每次失败后翻倍 |
| 最大退避 | 60 s | 退避上限 |
| 最大重试次数 | 无限 | 持续重试，不放弃 |
| 重连成功后重置退避 | 是 | 恢复为初始 1 s |

退避序列示例：1 s → 2 s → 4 s → 8 s → 16 s → 32 s → 60 s → 60 s → ...

### 4.3 断网期间行为

| 功能 | 行为 |
|---|---|
| 本地状态机 | 正常运行，不因断网停止 |
| RGB 显示 | 正常显示占用/模式/告警状态，附加白色慢闪 (0.5 Hz) 表示 Wi-Fi 断开 |
| 按键 | 正常响应，可切换 QUIET 模式 |
| 传感器 | 正常采集 |
| Matter 属性 | 不上报，保留本地最新值 |
| OLED（可选） | 显示 "Wi-Fi: Disconnected" |

### 4.4 重连后恢复

Wi-Fi 重连成功后按以下顺序恢复：

1. 标记 `wifi_connected = true`，RGB 停止白色慢闪。
2. 等待 Matter 会话恢复（最多 10 s）。
3. Matter 会话恢复后，强制上报首版两个业务属性（`occupancy`, `CurrentMode`）；环境告警仅本地显示，不上报不存在的 `stateValue`。
4. 如果 Matter 会话 10 s 内未恢复，记录告警，继续重试 Matter 连接。

### 4.5 错误密码处理

- Wi-Fi 连接返回 `WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT` 或 `WIFI_REASON_AUTH_FAIL` → 判定为密码错误。
- 密码错误不进入无限退避重连，而是：
  1. 重试 3 次（退避 1 s / 2 s / 4 s），确认不是临时问题。
  2. 3 次均失败 → RGB 红色快闪 (2 Hz)，等待用户长按重新配网。
  3. 不清除凭据（可能是路由器临时问题），但标记 `wifi_auth_failed = true`。

## 5. 恢复出厂设置

### 5.1 触发方式

| 方式 | 操作 | 防误触 |
|---|---|---|
| 按键长按 | 长按 ≥ 5 s | RGB 红色倒计时闪烁，5 s 内释放可取消 |
| Matter 控制器命令 | 通过 ESP-Matter 提供的 Factory Reset 接口（首版可选） | 需要已建立的 Matter 会话 |

### 5.2 执行步骤

1. 标记 `factory_reset_pending = true`。
2. 调用 ESP-Matter/CHIP 提供的 Factory Reset 接口（如 `chip::Server::GetInstance().ScheduleFactoryReset()`），由 Matter 栈负责：
   - 擦除 Fabric 信息
   - 擦除 commissioning 数据
   - 清理相关 NVS 命名空间
3. 业务代码额外擦除 NVS 中的 Wi-Fi 凭据和用户配置（`config` 命名空间）。
4. 擦除成功 → RGB 绿色常亮 1 s → 系统重启。
5. 擦除失败 → RGB 红色常亮，不重启，记录严重错误日志。

> **重要**：
> - Wi-Fi 凭据、Fabric、commissioning 状态不应由业务代码手工维护和擦除指定 NVS namespace。
> - 应使用 ESP-Matter 提供的 Factory Reset 接口，由 Matter 栈自行清理其内部数据。
> - 业务代码只负责清理自己创建的 NVS 命名空间（如 `config`）。

### 5.3 重启后行为

- NVS 无 Wi-Fi 凭据和 Matter 数据 → 自动进入 BLE Commissioning 模式。
- 用户配置已擦除 → 使用默认配置。
- 设备表现为首次上电状态。

## 6. 配置版本迁移

### 6.1 版本管理

- 每次配置结构变更时，递增 `CONFIG_VERSION`。
- NVS 中存储当前配置版本号。
- 启动时比较固件中的 `CONFIG_VERSION` 与 NVS 中存储的版本号。

### 6.2 迁移规则

| 场景 | 处理 |
|---|---|
| NVS 版本 = 固件版本 | 正常加载 |
| NVS 版本 < 固件版本 | 执行迁移函数链：v1→v2→...→current，每步验证 |
| NVS 版本 > 固件版本 | 记录告警，尝试加载兼容字段，不兼容字段使用默认值 |
| NVS 无版本号（首次或损坏） | 使用默认配置，写入当前版本号 |
| 迁移失败 | 回退默认配置，记录错误，不阻塞启动 |

### 6.3 迁移函数要求

- 每个迁移步骤必须幂等（可重复执行不产生副作用）。
- 迁移步骤失败不阻塞后续步骤（跳过失败字段，使用默认值）。
- 迁移完成后写入新版本号。

## 7. 断电重启行为

| 场景 | 行为 |
|---|---|
| 正常运行中断电 | 重启后从 NVS 恢复配置，状态机初始化为 VACANT + NORMAL + OK |
| 配网过程中断电 | 重启后检查 NVS：如有有效凭据 → 连接 Wi-Fi；如无 → 重新配网 |
| NVS 写入中断电 | NVS 事务保证原子性；如写入不完整，下次启动检测到校验失败 → 回退旧值或默认值 |
| OTA 过程中断电 | 见 [OTA 安全边界](ota-safety.md) |

## 8. Watchdog 复位行为

- TWDT 超时 → 打印所有任务状态 → 系统重启。
- 重启后在启动日志中标记 `reset_reason = TWDT`。
- 连续 3 次 TWDT 复位 → 进入安全模式：只启动传感器和 RGB，不启动 Wi-Fi/Matter，RGB 黄色常亮，等待用户操作。

## 9. 已知风险 / 未覆盖项

| 风险 | 说明 | 影响范围 | 跟踪 |
|------|------|----------|------|
| **实机 BLE commissioning 未验证** | Phase 3 Step 2 的 BLE commissioning 闭环仅在代码 + 编译层面验证通过，未在真实 ESP32-C6 硬件上使用 Matter Controller（chip-tool / ESP Matter Controller app）完成完整的 BLE pairing → PASE → NOC → Wi-Fi 凭据注入 → CASE 会话建立流程。Step 1 的 chip-tool 配网日志显示设备在 `FindOperationalForStayActive` 步骤失败（CHIP Error 0x00000046: No endpoint），Wi-Fi 凭据虽已写入但设备在 Wi-Fi 网络上的操作可达性未确认。 | BLE commissioning 闭环可能在实际硬件上存在 mDNS / IPv6 / Wi-Fi 切换时序问题。 | 待 Phase 3 Step 7（端到端验证）覆盖 |
| **Matter degraded 模式未实测** | `esp_matter::start()` 失败后部分 CHIP/NVS 状态无法完整回滚，当前靠 `s_node = nullptr` + 不 spawn `matter_adapter_task` 隔离。 | degraded 模式下 CHIP 内部状态可能残留，影响后续恢复。 | 待异常分支测试覆盖 |
| **BLE 释放后不可恢复** | `esp_bt_mem_release(ESP_BT_MODE_BLE)` 在 `kCommissioningComplete` 时调用，释放 ~20-30 KB。释放后 BLE 不可再启用（需重启）。如果后续所有 fabric 被移除并需要重新配网，设备必须冷重启才能重新启用 BLE。 | 重新配网需要额外的重启步骤。 | 已知设计权衡；工厂复位已处理重启路径 |

## 10. 版本与变更记录

## 10. 版本与变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 0.3 | 2026-07-22 | Phase 3 Step 5：fabric 计数逻辑（移除"多 fabric 未处理"风险）；BLE 内存释放；commissioning window 打开跟踪 |
| 0.2 | 2026-07-22 | 新增已知风险章节（实机 BLE commissioning 未验证等 3 项）；Phase 3 Step 3 完成后更新 |
| 0.1 | 2026-07-11 | 初版：BLE commissioning、Wi-Fi 重连、恢复出厂、配置迁移、断电/watchdog 复位 |
