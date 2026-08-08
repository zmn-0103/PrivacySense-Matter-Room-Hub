# PSRH-042 当前会话总结

日期：2026-08-08
协作角色：Builder AI
项目：PrivacySense-Matter-Room-Hub
分支：`agent/psrh-042-matter-v15`

## 1. 本轮任务边界

本轮严格采用最小增量验收范围：

- 保留阶段 1、阶段 2、W01 以及既有本地/Wi‑Fi PASS，不重复执行；
- 构建通过后优先完成真实 T14 BLE commissioning；
- commissioning 成功后读取一次 EP1/EP2，确认 Endpoint 可达；
- 仅补 T05、T16、T17 的 Matter/时段部分；
- T09、T10、T12、T15、T19 仅补各自 Matter 恢复/同步部分；
- 不执行额外烧录、擦除 NVS 或无关回归；
- 最终摘要只记录本轮新增结果，并引用旧证据。

原任务契约、批准范围、旧构建/烧录/启动证据和旧告警记录见：

- [`agent/tasks/PSRH-042.yml`](../agent/tasks/PSRH-042.yml)
- [`docs/session-summary-20260807.md`](session-summary-20260807.md)
- [`tests/evidence/PSRH-042_machine_verification_20260807.md`](../tests/evidence/PSRH-042_machine_verification_20260807.md)

## 2. 构建与代码状态

- 按用户要求限制并行任务为 `-j2`；文档命令为 `ninja -C firmware/build -j2`。
- 正式基线 `02e67aa` 的独立构建退出码为 `1`：旧 `ModeOptionStruct` API 在锁定 ESP-Matter 1.5 上于链接前失败；该失败日志已复制到仓库外持久目录。为得到可比增量，收口前兼容快照 `f6e9b6c` 完成 `1497/1497`，当前工作树按文档命令 `ninja -C firmware/build -j2` 退出码为 `0`。
- `git diff --check` 已通过。
- 本轮没有重新烧录；普通 flash 因此前置“不额外烧录”约束未获安全批准，当前板上运行镜像不是本轮修正后的镜像。
- 本轮代码收口修改位于 `firmware/main/matter_app.cpp`、`firmware/main/matter_app.h`、`firmware/main/state_machine.c`、`firmware/main/state_machine.h`：ChangeToMode 在 SDK 写入前同步等待状态机提交/拒绝；NIGHT 由 SupportedModesManager 和状态机双重校验；本地 EP1/EP2 投影使用 `attribute::report()`；报告失败通过代际 FORCE_SYNC 重试；QUIET→NIGHT 保存实际进入前模式。
- 资源结果：当前 Flash Code `1,751,112 B`、DIRAM `241,781 B`、LP SRAM `24 B`、BIN `1,904,000 B`；相对兼容参考 `f6e9b6c` 分别为 `+1,578 B`、`+200 B`、`+0 B`、`+1,584 B`。正式 `02e67aa` 因 API 不兼容无法给出数值增量。
- 当前 Host 测试日志实际为 `127/127 PASS`（17+20+10+21+25+34）；历史完整组件计数 `178/178 PASS` 不在本轮重跑。该套件没有覆盖 Matter 请求超时、迟到事件取消或请求槽复用路径，不对此类路径宣称 Host PASS。
- 既有项目告警仍保持记录，包括 CMake deprecation、未使用函数和上游告警；未将其误报为本轮新增问题。

## 3. WSL、USB 和网络环境

- WSL 使用 `mirrored` 网络模式，以保留 Matter 所需的 IPv6、多播和 mDNS 能力。
- Meta Fake-IP 导致的 WSL HTTPS 路由问题已记录在旧会话摘要中；WSL 使用本机代理端口 `7897`，并设置 Matter 本地发现不走代理的 `NO_PROXY`。
- Windows `usbipd` 已挂载 CP2102 串口和 Intel Bluetooth；设备断电后 CP2102 需要重新 attach。
- 控制器运行路径使用 mirrored 接口的 IPv6 operational discovery；本轮成功日志未再出现 NAT 阶段的 operational discovery 超时。
- 本轮使用的 `chip-tool`：

  `/home/administrator/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool`

## 4. T14 真实 BLE commissioning

### 4.1 失败尝试与原因

- 早期 WSL NAT 尝试在 `FindOperationalForStayActive` 阶段出现 `CHIP Error 0x00000032: Timeout`，未形成可用的 operational mDNS/IPv6 会话。
- mirrored 第一次重试时设备未处于可配网广播状态，结果为 BLE 扫描未找到目标设备。
- 设备长按进入配网状态后重新执行，使用独立临时存储目录保存控制器状态。

### 4.2 成功证据

历史成功日志目录（当前已不存在，原始日志不可恢复）：

`/tmp/psrh-042-t14-mirrored-20260808-r2/commission.log`

成功路径包含：

- `Pairing Success`；
- `PASE establishment successful`；
- Wi‑Fi Network Commissioning 成功；
- NOC chain 生成和验证成功；
- `Secure Pairing Success`、`CASE establishment successful`；
- `FindOperationalForStayActive` 成功；
- `FindOperationalForCommissioningComplete` 成功；
- `Commissioning complete ... success`；
- `Device commissioning completed with success`。

根据上一会话摘要，T14 成功路径曾通过 mirrored IPv6/mDNS 完成 BLE → PASE → Wi‑Fi 凭据 → NOC → CASE → operational discovery 闭环，且未出现此前关注的 `CHIP Error 0x00000046: No endpoint`。本次无法重新读取原始日志或计算其 SHA-256；该结果只能作为历史摘要，不能宣称原始日志已持久化。

成功完成后出现的 BLE unsubscribe/connection cleanup endpoint 文字属于收尾阶段日志，不影响 commissioning 结果，也不是 EP1/EP2 缺失。

## 5. commissioning 后 Endpoint 验证

上一会话使用 T14 控制器存储完成过一次性读取；原始控制器存储当前不可用：

- EP1 Occupancy：`1`；
- EP2 SupportedModes：`Normal=0`、`Quiet=1`、`Night=2`；
- EP2 CurrentMode：`2`。

上述读取结果保留为历史摘要；本次未能用既有 Fabric 重现，不能新增声称。

## 6. 本轮测试结果

| 测试 | 新增 Matter/时段结果 | 备注 |
|---|---|---|
| T05 | **PARTIAL** | 仅有雷达恢复后的 EP1 `Occupancy=1` 读取；没有断线前、断线期间、恢复后三个 Matter 读取点。 |
| T09 | **PASS** | 路由器恢复后重新完成 operational discovery/CASE，EP1 `Occupancy=1`、EP2 `CurrentMode=2`；旧本地/Wi‑Fi部分未重跑。 |
| T10 | **PASS（Matter子项）** | 独立 `chip-tool` 进程重新初始化控制器、恢复 Fabric、完成 CASE 后读取属性；未重复本地/Wi‑Fi部分。 |
| T12 | **PASS（Matter子项）** | 设备断电重启、USB 重新挂载后，EP1 `Occupancy=1`、EP2 `CurrentMode=2` 可读。 |
| T15 | **PASS（历史摘要，原始日志不可恢复）** | 上一会话摘要记录 Pairing/PASE/NOC/CASE/operational discovery/commissioning complete 成功及后续读取；本次不重跑。 |
| T19 | **PARTIAL（恢复后可读；同步未充分验证）** | 有恢复后的 Matter 读取摘要，但没有本次要求的“断网期间实际改变本地状态、恢复后读取最新值”闭环。 |

历史 T15 commissioning 原始日志目录（当前已不存在）：

`/tmp/psrh-042-t15-mirrored-20260808/commission.log`

上一会话摘要记录 T15 未出现 `0x00000046`。T15 重新配网后的 `CurrentMode=0` 是当时状态快照；本次不重跑且无法恢复原始日志。

## 7. 尚未完成或明确不宣称的项目

- T16：**DEFERRED**。由于既有 chip-tool Fabric 存储不存在，本次未执行“初始 0 → 短按后 1 → 再次短按后 0”的控制器读取闭环。
- NIGHT 防护：**DEFERRED**。代码和 Host/构建证据存在，但没有执行真实 `ChangeToMode(2)` 并取得控制器失败响应；不能将代码路径当作 HIL PASS。
- T17：按用户要求跳过。W01 只证明 SNTP 已同步，不证明 NIGHT 自动进入或 07:00 自动退出，因此没有声称 T17 自动转换 PASS。
- T19/T14/T15 中出现的 `CurrentMode=2` 只作为状态快照，不作为 T17 自动切换证据。
- 旧任务中的 warning gate、reviewer sign-off 及其他未完成事项仍以旧会话摘要为准。

## 8. 新增验收文件

本轮脱敏增量验收摘要已保存：

[`tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md`](../tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md)

该文件只记录本轮新增的 T14、Endpoint、T05、T09、T10、T12、T15、T19 结果，并明确 T16/T17 的边界；没有写入 Wi‑Fi 凭据、MAC、完整 IPv6 地址或原始控制器日志。

## 9. 工作区与证据保护

- 原有代码和证据变更均保留，未执行破坏性清理或 reset。
- T14/T15 原始日志此前保留在 `/tmp`，现已被清理；无法复制到持久目录或计算 SHA-256。未提交任何原始日志、Fabric 存储或敏感信息。
- 宿主只读搜索未找到既有 `chip_tool_config*.ini`；未创建空存储、未重新 commissioning。
- 本次非破坏性串口观察只记录运行心跳；持久副本位于 `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/closeout/serial-observation-20260808.log`，SHA-256 为 `91d67b46637f067eed253146ba1b2c22254cb0fb31145ce0c53e0a8b7c555029`，不作为 T16/T05/T19 PASS 证据。
- 当前构建、size、Host 测试、正式基线失败和兼容参考日志均直接保存在上述 `closeout/` 持久目录；没有新的日志依赖外部 `/tmp`。
- 分支历史中的 `c341e51` 仅修改 `AGENTS.md`、`agent/task_templates/task-contract.yml`、`docs/multi-agent-development.md`，不属于 PSRH-042 授权交付范围；未改写历史，整分支合并前需 Human Lead 单独接受或由有权限者拆分。
- 没有执行 `idf.py flash`、`erase-flash`、NVS 擦除或重新 commissioning。
- 当前文件的格式检查：`git diff --check` 通过。
