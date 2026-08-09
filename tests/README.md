# 测试与证据索引

本页是当前项目的证据地图，不是新的测试结果。PSRH-044 只整理索引和状态，
没有重新编译、烧录、占用串口、执行 HIL，也没有补测 T01、T02 或 T18。

## 1. 读取规则

- **PASS**：指定证据集中的测试/门禁通过；不自动扩展到其他硬件、镜像或场景。
- **PARTIAL**：只证明记录的子集，缺口必须跟随表格保留。
- **DEFERRED/BLOCKED**：没有在本次收口中补测，不能写入简历为已通过。
- **Historical / not rerun**：历史记录可作为背景，但不是 PSRH-044 新验证。
- 证据文件只提交脱敏 Markdown；原始日志、构建产物、Fabric storage 和 manifest
  位于任务契约记录的仓库外受控目录。

最终收口说明见 [docs/project-closeout.md](../docs/project-closeout.md)；求职表述
见 [docs/resume-materials.md](../docs/resume-materials.md)。

## 2. 当前任务级证据

### PSRH-042 — Matter API compatibility

任务契约：[agent/tasks/PSRH-042.yml](../agent/tasks/PSRH-042.yml)（当前 `DONE`）。
实现/审查范围绑定 ESP-Matter release/v1.5、EP0/EP1/EP2 和批准的模式/离线语义。

| 文件 | 用途 | 当前读取方式 |
|---|---|---|
| [PSRH-042_builder_handoff.md](evidence/PSRH-042_builder_handoff.md) | Builder 交接、提交绑定、构建/Host/HIL/审查汇总 | 独立 Reviewer PASS；T14/T17/T19/设备重启端点配对仍按 PARTIAL 读取 |
| [PSRH-042_matter_delta_acceptance_20260808.md](evidence/PSRH-042_matter_delta_acceptance_20260808.md) | new Fabric HIL 子项 | T05、T16、NIGHT guard 和 Controller success path 有 PASS；T14/T17/T19 保留 PARTIAL |
| [PSRH-042_controller_change_to_mode_hil_20260809.md](evidence/PSRH-042_controller_change_to_mode_hil_20260809.md) | 默认生产镜像 Controller success path | 两轮 `0→1→0`，证据提交 `0dda51d4`；早期 `87a3f41` 记录已标为 superseded |
| [PSRH-042_machine_verification_20260807.md](evidence/PSRH-042_machine_verification_20260807.md) | 早期机器验证记录 | 历史背景；不覆盖后续 Reviewer/Human Lead 收口 |
| [PSRH-042_preflight_20260807.md](evidence/PSRH-042_preflight_20260807.md) | 任务批准前置检查 | 历史 preflight；其中“未执行”描述不应被误读为当前最终状态 |

PSRH-042 的独立审查结论为 `gpt-5.6-sol PASS`，Human Lead 于 2026-08-09 接受
剩余证据边界。`READY_TO_MERGE` 是 handoff 的时间点记录；当前治理状态以任务
契约中的 `DONE` 为准。

### PSRH-043 — Phase 5 reliability/resource closure

任务契约：[agent/tasks/PSRH-043.yml](../agent/tasks/PSRH-043.yml)（当前 `DONE`）。

| 文件 | 用途 | 当前读取方式 |
|---|---|---|
| [PSRH-043_builder_handoff.md](evidence/PSRH-043_builder_handoff.md) | 集成范围、实现摘要、审查和最终 handoff | 绑定 `c2a0ff0`、`0d403f1`、精确 App BIN 和最终审查 |
| [PSRH-043_build_result.md](evidence/PSRH-043_build_result.md) | fresh baseline/integrated build | integrated `1498/1498`，exit 0；baseline `1497/1497` |
| [PSRH-043_host_test_result.md](evidence/PSRH-043_host_test_result.md) | Host suite | `130/130 PASS`，GCC 15.2.0，`-Wall -Wextra -Werror` |
| [PSRH-043_resource_measurement.md](evidence/PSRH-043_resource_measurement.md) | size、静态资源、快照和 artifact identity | App BIN `+1,472 B`，DIRAM/`.bss` `+1,920 B`，快照字段完整 |
| [PSRH-043_warning_classification.md](evidence/PSRH-043_warning_classification.md) | warning 分类 | 项目自有 unused-function warning 在集成 log 中消失；其余 warning 保留并分类 |
| [PSRH-043_hardware_deferral.md](evidence/PSRH-043_hardware_deferral.md) | 最终精确镜像 HIL | PSRH-043 请求的 BLE、DHT22、Wi-Fi、Matter 和真实断电恢复门禁均记录 PASS |

## 3. 测试矩阵与边界

| ID | 当前证据状态 | 可核实摘要 | 重要边界 |
|---|---|---|---|
| T01 | **DEFERRED** | 短时经过误触发 | PSRH-044 未补测；不能写成 PASS |
| T02 | **DEFERRED** | 静坐不动保持 OCCUPIED | PSRH-044 未补测；不能写成 PASS |
| T03 | Historical PASS | 持续有人后离开 | 早期硬件记录；非本轮新验证 |
| T04 | Historical PASS | 持续无人后进入 | 早期硬件记录；非本轮新验证 |
| T05 | **PASS（PSRH-042 new Fabric 子集）** | Occupancy `1→1→1` across radar disconnect/recovery | 该结论限于记录的 new Fabric、镜像和读取序列 |
| T06 | Historical PASS | 雷达离线后恢复 | 非本轮新验证 |
| T07 | **PASS（分层证据）** | 早期 DHT22 parser/smoke 记录；PSRH-043 精确镜像还记录正常/故障/恢复 | 不把单次环境样本扩展为长期传感器可靠性 |
| T08 | Not claimed | 错误 Wi-Fi 密码路径 | 当前收口不新增 PASS 判定 |
| T09 | Historical / PSRH-043 recovery evidence | 路由/AP 断开恢复 | PSRH-043 是受控 AP outage；不是所有网络拓扑 |
| T10 | Historical / PSRH-043 recovery evidence | Controller/CASE 恢复和属性读取 | 限于记录的 controller storage 和镜像 |
| T11 | Not claimed | 配置损坏恢复 | 需要单独批准的破坏性注入；PSRH-044 不执行 |
| T12 | **PASS（PSRH-043 final HIL）** | 真实 USB 断电后 Fabric/CASE/属性/传感器恢复 | 一次记录的精确镜像观察；不等于产品级断电安全 |
| T13 | Observation only | 运行窗口没有观察到 TWDT failure；策略未改动 | 未执行故意 watchdog fault injection，不写成 TWDT reset PASS |
| T14 | **PARTIAL（PSRH-042 evidence boundary）** | operational CASE/reads 被保留 | 该证据集的 raw PASE/NOC success chain 未保留；后续 PSRH-043 有另一组授权 BLE evidence |
| T15 | Historical / PSRH-043 authorized re-commissioning | 重新 commissioning 相关记录 | 不把历史摘要与 PSRH-042 T14 合并改写 |
| T16 | **PASS** | `CurrentMode 0→1→0`，按键/Controller 读写记录 | 不扩展到所有 UI 或生态兼容性 |
| T17 | **PARTIAL** | 自动退出和退出后 `CurrentMode=0` | 自动进入未单独捕获 |
| T18 | **BLOCKED** | 环境告警需要可控热源 | PSRH-044 明确不补测；不能写成 PASS |
| T19 | **PARTIAL** | 离线本地 `NORMAL→QUIET` | PSRH-042 证据集未验证 AP 恢复后的最新 Matter 值读取 |
| T20 | Not run in closeout | 24 h 长时间稳定性 | 不用短时资源快照替代 24 h 结论 |

## 4. 历史模块/可靠性记录

这些文件是早期或阶段性证据，适合追溯实现背景；它们不改变 PSRH-042/043
最终任务契约和边界。

| 文件 | 主题 |
|---|---|
| [R07_config_persistence_test.md](evidence/R07_config_persistence_test.md) | 配置持久化测试记录 |
| [R08_stop_timeout_fault_inject_test.md](evidence/R08_stop_timeout_fault_inject_test.md) | 雷达 stop 超时负向测试 |
| [R09_production_regression_test.md](evidence/R09_production_regression_test.md) | 早期 10 分钟生产回归 |
| [R10_radar_uart_disconnect_test.md](evidence/R10_radar_uart_disconnect_test.md) | 雷达 UART 断线/恢复 |
| [R11_radar_config_save_test.md](evidence/R11_radar_config_save_test.md) | LD2410C 配置事务与保存 |
| [R12_phase1_acceptance_record.md](evidence/R12_phase1_acceptance_record.md) | 阶段 1 硬件单元验收 |
| [T07_DHT22_RANGE5_smoke_20260719.md](evidence/T07_DHT22_RANGE5_smoke_20260719.md) | DHT22 解析/阈值 smoke |
| [W01_network_reconnect_acceptance_20260721.md](evidence/W01_network_reconnect_acceptance_20260721.md) | Wi-Fi 重连验收 |
| [dev-env-config-2026-07-17.md](evidence/dev-env-config-2026-07-17.md) | 开发环境与工具链记录 |

## 5. PSRH-043 外部 artifact 索引

原始文件不在 Git 中；以下路径和 manifest hash 已在任务契约、HIL 证据和
Reviewer summary 中重复绑定：

| Artifact 集 | 仓库外路径 | Manifest SHA-256 |
|---|---|---|
| Integrated closeout | `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout` | `929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143` |
| Authorized BLE | `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/ble-direct-20260809` | `4ef5aa8bd65f3f4dc0ff8f9cc5ccb6e3ccb4957a64c8e0198d631fc4e0b5b6fb` |
| Final HIL closeout | `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/hil-closeout-20260809` | `1792415f3734a81147d349d384999f0cf454c07ef6a75ea48d4f71e82e5362fb` |

App BIN SHA-256：
`3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c`。

## 6. 证据文件规范与安全边界

- 提交 Markdown 摘要，不提交 `.log` 原始串口/控制器输出。
- 摘要必须移除 Wi-Fi 密码、Matter setup payload、私钥、MAC、完整地址和
  Fabric/Node 标识；只保留必要状态、退出码、时间、提交和 hash。
- “artifact manifest 完整性通过”是既有 Reviewer 对仓库外文件的记录，不是
  PSRH-044 重新执行的 hash 校验。
- 任何固件、测试实现、镜像、manifest 或实质证据变化都需要按任务流程重新
  Review；文档索引不能扩大原证据范围。

## 7. PSRH-044 检查边界

本任务完成时只执行：

1. changed-file scope 检查；
2. Markdown 相对链接检查；
3. `git diff --check` 和事实/数字/状态交叉检查；
4. 独立 Reviewer 审查。

不执行构建、Host 测试、flash、HIL、T01/T02/T18 或任何硬件操作。
