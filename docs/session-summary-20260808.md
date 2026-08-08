# PSRH-042 当前会话总结

日期：2026-08-08—2026-08-09
协作角色：Builder AI
项目：PrivacySense-Matter-Room-Hub
分支：`agent/psrh-042-matter-v15`

固定审查目标：实现提交 `2392a3b`、`b9dee96`，元数据提交 `5401483`、`5872435`；协作规范提交 `c341e51` 是单独同步的治理文档，不算 PSRH-042 功能交付。P1 独立审查按 Human Lead 指令跳过，本摘要不声称 Reviewer sign-off。

## 1. 本轮任务边界

本轮严格采用最小增量验收范围：

- 保留阶段 1、阶段 2、W01 以及既有本地/Wi‑Fi PASS，不重复执行；
- 本轮真实结果另行标记为“新 Fabric HIL”；不得将其替代或伪装为昨天既有 Fabric 的恢复证据；
- 新 Fabric HIL 使用持久 controller storage；不在提交、日志或摘要中保存 setup payload、密码、私钥、MAC 或完整 IPv6；
- commissioning 后读取 EP1/EP2，确认 Endpoint 可达；随后执行 T05、T16、NIGHT、T17、T19 及恢复子项；
- T09、T10、T12、T15 的旧 Fabric 结果不重跑、不与本轮新 Fabric HIL 合并计数；
- 仅在用户明确授权后烧录 5 分钟 HIL 变体；不执行额外擦除 NVS 或无关回归；
- 最终摘要只记录本轮新增结果，并引用旧证据。

原任务契约、批准范围、旧构建/烧录/启动证据和旧告警记录见：

- [`agent/tasks/PSRH-042.yml`](../agent/tasks/PSRH-042.yml)
- [`docs/session-summary-20260807.md`](session-summary-20260807.md)
- [`tests/evidence/PSRH-042_machine_verification_20260807.md`](../tests/evidence/PSRH-042_machine_verification_20260807.md)

## 2. 构建与代码状态

- 按用户要求限制并行任务为 `-j2`；文档命令为 `ninja -C firmware/build -j2`。
- 正式基线 `02e67aa` 的独立构建退出码为 `1`：旧 `ModeOptionStruct` API 在锁定 ESP-Matter 1.5 上于链接前失败；该失败日志已复制到仓库外持久目录。为得到可比增量，收口前兼容快照 `f6e9b6c` 完成 `1497/1497`；本轮超时修正提交 `b9dee96` 的持久外部 clean 源树也完成 `1497/1497`，当前工作树按文档命令 `ninja -C firmware/build -j2` 退出码为 `0`，最新脱敏日志哈希为 `eb9358823c5af9346aeab7cb47014af704d8665f85f5decacd356f4090b36b26`。
- `git diff --check` 已通过。
- 用户已明确授权烧录 5 分钟 HIL 变体；使用受限 `ninja -C <external-hil-build-dir> -j2` 构建并烧录，未执行 `erase-flash`，NVS/Fabric 保留。
- 本轮代码收口修改位于 `firmware/main/matter_app.cpp`、`firmware/main/matter_app.h`、`firmware/main/state_machine.c`、`firmware/main/state_machine.h`：ChangeToMode 在 SDK 写入前同步等待状态机提交/拒绝；超时请求保持占用并在迟到事件处理前取消；NIGHT 由 SupportedModesManager 和状态机双重校验；本地 EP1/EP2 投影使用 `attribute::report()`；报告失败通过代际 FORCE_SYNC 重试；QUIET→NIGHT 保存实际进入前模式。
- 资源结果：当前 clean BIN `1,904,896 B`、Flash Code `1,752,010 B`、DIRAM `241,781 B`、LP SRAM `24 B`；相对兼容参考 `f6e9b6c` 分别为 `+2,480 B`、`+2,476 B`、`+200 B`、`+0 B`。正式 `02e67aa` 因 API 不兼容无法给出数值增量。
- 当前 Host 测试日志实际为 `127/127 PASS`（17+20+10+21+25+34）；本次复跑日志哈希为 `5192e245d11802a0663c014caacb4ca8bb0a000639c60d91e8d16c39c9627b50`。该套件没有覆盖 Matter 请求超时、迟到事件取消或请求槽复用路径，不对此类路径宣称 Host PASS；测试代码不在本任务 `owned_paths` 中，是否新增覆盖需由 Reviewer/人类决定。
- 既有项目告警仍保持记录，包括 CMake deprecation、未使用函数和上游告警；未将其误报为本轮新增问题。

## 3. WSL、USB 和网络环境

- WSL 使用 `mirrored` 网络模式，以保留 Matter 所需的 IPv6、多播和 mDNS 能力。
- Meta Fake-IP 导致的 WSL HTTPS 路由问题已记录在旧会话摘要中；WSL 使用本机代理端口 `7897`，并设置 Matter 本地发现不走代理的 `NO_PROXY`。
- Windows `usbipd` 已挂载 CP2102 串口和 Intel Bluetooth；设备断电后 CP2102 需要重新 attach。
- 控制器运行路径使用 mirrored 接口的 IPv6 operational discovery；本轮成功日志未再出现 NAT 阶段的 operational discovery 超时。
- 本轮使用的 `chip-tool`：

  `/home/administrator/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool`

## 4. 新 Fabric HIL 结果

本轮新 Fabric HIL 脱敏证据位于仓库外持久目录：

`/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/new-fabric-hil/sanitized-evidence/`

closeout controller storage manifest snapshot SHA-256：
`22711e42d0da1c19496e4c78205ffdf6d963c3c91b28120f448c409461d0220b`。

| 项目 | 结果 | 证据边界 |
|---|---|---|
| T14 新 Fabric commissioning/operational | **PARTIAL** | CASE/Operational Discovery/EP1/EP2 读取成功；原始 PASE/NOC 成功链未保留。 |
| T05 Occupancy | **PASS** | 断线前、中、恢复后三个 EP1 读取为 `1 → 1 → 1`。 |
| T16 | **PASS** | 短按后控制器读取为 `0 → 1 → 0`。 |
| NIGHT 防护 | **PASS** | 窗口外 `ChangeToMode(2)` 失败，脱敏错误码 `0x0000002F`。 |
| T17 | **PARTIAL** | 5 分钟 HIL 定时退出 `NIGHT → NORMAL` 已捕获；自动进入未单独捕获。 |
| T19 | **PARTIAL** | 离线 `NORMAL→QUIET` 已捕获；恢复后 Operational mDNS 未发现，最新值读取未完成。 |
| 控制器重启恢复 | **PASS** | 独立 chip-tool 进程复用同一 storage 并重新 CASE。 |
| 设备重启后读取 | **PARTIAL** | 普通重启后 CASE/EP2 可读；重启后 EP1/EP2 双端点配对读取未保留。 |

5 分钟 HIL 编译定义为 `PSRH_HIL_NIGHT_EXIT_AFTER_MS=300000`，生产默认 NIGHT
窗口仍为 `22:00–07:00`。本轮未重新 commissioning。

## 5. 历史 T14 真实 BLE commissioning

### 5.1 失败尝试与原因

- 早期 WSL NAT 尝试在 `FindOperationalForStayActive` 阶段出现 `CHIP Error 0x00000032: Timeout`，未形成可用的 operational mDNS/IPv6 会话。
- mirrored 第一次重试时设备未处于可配网广播状态，结果为 BLE 扫描未找到目标设备。
- 设备长按进入配网状态后重新执行，使用独立临时存储目录保存控制器状态。

### 5.2 成功证据

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

## 6. commissioning 后历史 Endpoint 验证

上一会话使用 T14 控制器存储完成过一次性读取；原始控制器存储当前不可用：

- EP1 Occupancy：`1`；
- EP2 SupportedModes：`Normal=0`、`Quiet=1`、`Night=2`；
- EP2 CurrentMode：`2`。

上述读取结果保留为历史摘要；本次未能用既有 Fabric 重现，不能新增声称。

## 7. 本轮测试结果

| 测试 | 新增 Matter/时段结果 | 备注 |
|---|---|---|
| T05 | **PASS** | 新 Fabric HIL 已保留断线前、中、恢复后三个 EP1 `Occupancy=1` 读取点。 |
| T09 | **PASS** | 路由器恢复后重新完成 operational discovery/CASE，EP1 `Occupancy=1`、EP2 `CurrentMode=2`；旧本地/Wi‑Fi部分未重跑。 |
| T10 | **PASS（Matter子项）** | 独立 `chip-tool` 进程重新初始化控制器、恢复 Fabric、完成 CASE 后读取属性；未重复本地/Wi‑Fi部分。 |
| T12 | **PASS（Matter子项）** | 设备断电重启、USB 重新挂载后，EP1 `Occupancy=1`、EP2 `CurrentMode=2` 可读。 |
| T15 | **PASS（历史摘要，原始日志不可恢复）** | 上一会话摘要记录 Pairing/PASE/NOC/CASE/operational discovery/commissioning complete 成功及后续读取；本次不重跑。 |
| T16 | **PASS** | 新 Fabric HIL 已完成 `0 → 1 → 0` 控制器读取闭环。 |
| NIGHT 防护 | **PASS** | 新 Fabric HIL 取得窗口外真实 `ChangeToMode(2)` 失败响应。 |
| T17 | **PARTIAL** | 新 Fabric HIL 捕获自动退出，但未单独捕获自动进入。 |
| T19 | **PARTIAL** | 离线期间本地 `NORMAL→QUIET` 已捕获；恢复后 mDNS 未发现，最新值读取未证实。 |

历史 T15 commissioning 原始日志目录（当前已不存在）：

`/tmp/psrh-042-t15-mirrored-20260808/commission.log`

上一会话摘要记录 T15 未出现 `0x00000046`。T15 重新配网后的 `CurrentMode=0` 是当时状态快照；本次不重跑且无法恢复原始日志。

## 8. 尚未完成或明确不宣称的项目

- T14：**PARTIAL**。本轮 operational CASE/读取成功，但原始 PASE/NOC 成功链未保留。
- T17：**PARTIAL**。本轮仅直接捕获 5 分钟 HIL 变体的自动退出；不将既有 NIGHT 状态快照冒充自动进入证据。
- T19：**PARTIAL**。AP 恢复后 `_matter._tcp` 未发现，控制器读取超时；不把 AP 恢复动作冒充最新值同步 PASS。
- 设备重启后的 EP1/EP2 双端点读取：**PARTIAL**；普通重启后 CASE 与 EP2 可读，但本次未同时保留 EP1/EP2。
- 重新 commissioning：**DEFERRED**；本轮无必要，不再次清除 NVS/Fabric。
- T19/T14/T15 中出现的 `CurrentMode=2` 只作为状态快照，不作为 T17 自动切换证据。
- 旧任务中的 warning gate、reviewer sign-off 及其他未完成事项仍以旧会话摘要为准。

## 9. 新增验收文件

本轮脱敏增量验收摘要已保存：

[`tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md`](../tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md)

该文件记录本轮新 Fabric HIL 的 T05/T14/T16/NIGHT/T17/T19 与恢复子项，明确 PASS/PARTIAL/DEFERRED 边界；没有写入 Wi‑Fi 凭据、MAC、完整 IPv6 地址或原始控制器日志。

## 10. 工作区与证据保护

- 原有代码和证据变更均保留，未执行破坏性清理或 reset。
- T14/T15 原始日志此前保留在 `/tmp`，现已被清理；无法复制到持久目录或计算 SHA-256。未提交任何原始日志、Fabric 存储或敏感信息。
- 本轮使用新 Fabric 的持久 storage；未创建空 storage，未在本轮再次 commissioning。
- 新 Fabric HIL 的 T05/T16/NIGHT/T17/T19 已依赖真实串口和同一 storage 执行；T14/T17/T19 的 PARTIAL 边界见本摘要及脱敏证据文件。
- 当前增量构建、clean build/size、Host 测试、正式基线失败和兼容参考日志均直接保存在 `.../20260808/` 持久目录（clean build 根目录为 `clean-build-b9dee96/`）；没有新的日志依赖外部 `/tmp`。
- 分支历史中的 `c341e512600e673ca23fb73b377a4a8052d1b3a1`（短哈希 `c341e51`）仅修改 `AGENTS.md`、`agent/task_templates/task-contract.yml`、`docs/multi-agent-development.md`，不属于 PSRH-042 授权交付范围；未改写历史，整分支合并前需 Human Lead 单独接受或由有权限者拆分。
- 恢复出厂曾获明确授权并执行；5 分钟 HIL 镜像也获明确授权并烧录，但未执行 `erase-flash`。烧录后未再次擦除 NVS 或重新 commissioning。
- 当前文件的格式检查：`git diff --check` 通过。
