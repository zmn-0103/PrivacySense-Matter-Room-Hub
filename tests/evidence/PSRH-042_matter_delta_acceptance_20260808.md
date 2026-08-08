# PSRH-042 Matter 增量验收摘要

日期：2026-08-08
范围：仅记录本轮新增的真实 Matter 控制器结果；阶段 1、阶段 2、W01，以及既有本地/Wi‑Fi 证据均不重做。

## 结果

| 项目 | 新增结果 | 脱敏证据摘要 |
|---|---|---|
| T14 首次 BLE 配网 | **PASS（历史摘要，原始日志不可恢复）** | 上一会话摘要记录 BLE 配对、PASE、Wi‑Fi 凭据注入、NOC、CASE、Operational IPv6/mDNS 发现和 commissioning complete 成功；本次未重跑。 |
| T14 后 Endpoint 可达性 | **PASS（历史摘要，原始控制器存储不可恢复）** | 上一会话摘要记录 EP1 `Occupancy=1`；EP2 `SupportedModes` 为 `Normal=0`、`Quiet=1`、`Night=2`；EP2 `CurrentMode=2`。 |
| T05 Matter 子项 | **PARTIAL** | 只有雷达恢复后的 EP1 `Occupancy=1` 读取；缺少断线前、断线期间、恢复后三个 Matter 读取点。 |
| T09 Matter 恢复/同步 | **PASS** | 路由器恢复后重新完成 operational discovery 和 CASE，EP1 `Occupancy=1`、EP2 `CurrentMode=2`。 |
| T10 Matter 控制器恢复 | **PASS** | 独立 `chip-tool` 进程重新初始化控制器、恢复 Fabric、完成 CASE 后读取 EP1/EP2；未重复本地/Wi‑Fi部分。 |
| T12 Matter 恢复/同步 | **PASS** | 设备断电重启并重新挂载后，控制器通过 Operational IPv6/CASE 读取 EP1 `Occupancy=1`、EP2 `CurrentMode=2`。 |
| T15 恢复出厂后重新配网 | **PASS（历史摘要，原始日志不可恢复）** | 上一会话摘要记录 Pairing/PASE、NOC/CASE、Operational discovery 和 commissioning complete 成功及后续读取；本次不重跑。 |
| T19 Matter | **PARTIAL（恢复后可读；同步未充分验证）** | 仅有恢复后的 Matter 读取摘要；没有实际断网期间改变本地状态、恢复后读取最新值的闭环。 |

## 明确未声称的结果

- T16 **DEFERRED**：没有既有 chip-tool Fabric 存储，未完成初始 `CurrentMode=0`、短按后 `1`、再次短按后 `0` 的控制器读取闭环。
- NIGHT 防护 **DEFERRED**：未取得真实控制器对 `ChangeToMode(2)` 的失败响应；不能将代码/Host 证据写成 HIL PASS。
- T17 按用户要求跳过；W01 只证明 SNTP 已同步，不证明 NIGHT 自动进入或退出。
- T19/T14/T15 中出现的 `CurrentMode=2` 仅作为当时状态快照，不作为 T17 自动转换证据。
- 上一会话的成功 commissioning 摘要和后续读取均未出现 `CHIP Error 0x00000046: No endpoint`；该历史摘要描述了 BLE → PASE → Wi‑Fi 凭据 → NOC → CASE → mDNS/IPv6 操作发现路径，本次没有重现它。

## 证据边界与引用

- T14/T15 原始控制器日志此前位于 `/tmp`，当前已不存在，无法复制或计算 SHA-256；本摘要不包含 Wi‑Fi 凭据、MAC、完整地址或原始日志。
- 宿主未找到既有 `chip_tool_config*.ini`，未创建空 Fabric、未重新 commissioning。
- 旧构建、烧录、启动、Host 测试和既有告警状态见 [PSRH-042 machine verification](PSRH-042_machine_verification_20260807.md)。
- 旧会话契约、批准范围和历史证据见 [session summary](../../docs/session-summary-20260807.md)。
- 既有 Wi‑Fi/SNTP 重连证据见 [W01 network reconnect acceptance](W01_network_reconnect_acceptance_20260721.md)。
