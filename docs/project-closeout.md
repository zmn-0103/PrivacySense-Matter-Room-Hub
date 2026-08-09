# PSRH-044 项目最终收口说明

日期：2026-08-09
任务：`PSRH-044` — Documentation and career-material closeout
基线：`e1db746171498be76770e5a7b2ab4456017c2ef5`
分支：`agent/psrh-044-docs-career-closeout`
类型：**文档-only**

## 1. 收口结论

PSRH-042 和 PSRH-043 的实现、构建、测试、资源和 HIL 证据已在指定基线中
完成集成，并在本次文档收口中将任务契约状态从 `READY_TO_MERGE` 更新为
`DONE`。本次收口没有重新解释既有结果，也没有把不同任务的证据合并成更强
的结论。

PSRH-044 本身只新增/更新任务记录、项目文档、求职材料和测试索引。链接、
格式和事实一致性检查完成后，由独立 Reviewer 对 `ff7e60c` 复审并给出 PASS；
Human Lead 随后明确接受。Builder 未自行伪造或代替 Reviewer 的结论。

## 2. Human Lead 的 Phase 6 决定

原计划的 Phase 6 同时包含样机整理和展示材料。Human Lead 于 2026-08-09
决定将其调整为“文档与职业材料收口”：

**纳入本次交付**

- 当前 README、项目计划书和最终收口说明。
- 测试/证据索引，以及 PSRH-042/043 当前任务状态整理。
- 中文/英文简历项目描述、面试讲解提纲和可核实量化成果清单。
- 所有职业陈述的提交/证据追溯，以及已知限制和“不宣称”边界。

**明确排除本次交付**

- 固件或测试实现修改。
- 重新构建、烧录、HIL，或 T01、T02、T18 补测。
- 洞洞板、外壳、线束、传感器安装和装配整理。
- 架构图、状态转换图、演示视频、照片和其他视觉展示材料。

因此，本文件中的“完成”仅表示文档/证据收口完成；不表示物理样机或产品化
阶段完成。

## 3. 任务状态与历史语境

| 任务 | 本次收口后的状态 | 独立审查/人工决定 | 需要保留的边界 |
|---|---|---|---|
| PSRH-042 | **DONE** | `gpt-5.6-sol` PASS；Human Lead 于 2026-08-09 接受 | T14、T17、T19 和设备重启后的 EP1/EP2 配对证据仍是 PARTIAL；见 [契约](../agent/tasks/PSRH-042.yml) |
| PSRH-043 | **DONE** | `gpt-5.6-sol` PASS；Human Lead 于 2026-08-09 接受 | 绑定精确 App BIN 与三套 external manifest；见 [契约](../agent/tasks/PSRH-043.yml) |
| PSRH-044 | **READY_TO_MERGE** | `gpt-5.6-sol` 对 `ff7e60c` 复审 PASS；Human Lead 于 2026-08-09 接受 | 不新增运行验证；见 [契约](../agent/tasks/PSRH-044.yml) |

PSRH-042 的 handoff 和 PSRH-043 的 HIL 文件中保留了它们在各自时间点的
`READY_TO_MERGE` 表述。这些是历史记录，不与当前任务契约的 `DONE` 冲突；
当前状态以本次任务契约和本文件为准。

## 4. 已交付实现的可审计摘要

PSRH-044 不改动以下实现；它只把已有事实整理成可复用的项目叙述。

| 主题 | 可宣称事实 | 主要提交/证据 |
|---|---|---|
| Matter 适配 | 在锁定 ESP-Matter release/v1.5 语义下，维护 EP0/EP1/EP2 拓扑；EP2 的 `NORMAL=0`、`QUIET=1`、`NIGHT=2` 及批准的离线语义保持记录 | implementation `2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`；[PSRH-042 handoff](../tests/evidence/PSRH-042_builder_handoff.md) |
| 异步边界 | 对 Matter `ChangeToMode` 使用有界状态机请求/应答，处理超时/取消/失败，并保留 retry-safe 的同步请求语义 | `2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`；[PSRH-042 handoff](../tests/evidence/PSRH-042_builder_handoff.md) |
| Controller 闭环 | 在默认生产镜像上完成 `CurrentMode 0 → ChangeToMode(1) → 1 → ChangeToMode(0) → 0`，并重复一次验证 request-slot reuse | evidence commit `0dda51d4aeef2a1717445dcb96c462094b8ed1ec`；[HIL 摘要](../tests/evidence/PSRH-042_controller_change_to_mode_hil_20260809.md) |
| 可靠性诊断 | 增加固定容量的任务快照和 heap/reset/HWM 诊断；不改变 watchdog、reset、safe-mode、OTA、分区或离线策略 | firmware merge `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`；[PSRH-043 handoff](../tests/evidence/PSRH-043_builder_handoff.md) |
| 构建/Host | fresh compatible baseline 与 integrated image 均有记录；integrated Host suite 为 130/130 PASS，ESP32-C6 目标为 1498/1498 PASS | evidence commit `0d403f13ec0a0e4b2c32e16f35893f987606ae1d`；[build](../tests/evidence/PSRH-043_build_result.md)、[Host](../tests/evidence/PSRH-043_host_test_result.md) |
| 运行时资源 | 观测快照满足 `tasks <= 32`、`captured == tasks`、`truncated=no`；周期快照为 16/16，capacity 32 | [resource measurement](../tests/evidence/PSRH-043_resource_measurement.md)；[HIL evidence](../tests/evidence/PSRH-043_hardware_deferral.md) |
| 精确镜像 HIL | PSRH-043 记录了授权 BLE re-commissioning、DHT22 故障/恢复、受控 Wi-Fi 断开/恢复、Matter CASE/属性读取和真实 USB 断电恢复 | evidence commits `a21cf00bfd8c01162cb3e47977e0e5d67ff82a70`、`6456d35089101a1fe5c947e2bb77f41920f0af16`；[HIL evidence](../tests/evidence/PSRH-043_hardware_deferral.md) |

## 5. 量化结果与证据绑定

下表中的数字可在提交后的 Markdown 证据和仓库外 manifest 中复核。它们不是
PSRH-044 新测量值。

| 指标 | 结果 | 参照/解释 | 证据 |
|---|---:|---|---|
| 集成 Host tests | `130/130 PASS` | `-Wall -Wextra -Werror`，包含 `health_diag` 三项 | [PSRH-043_host_test_result.md](../tests/evidence/PSRH-043_host_test_result.md) |
| 集成目标构建 | `1498/1498` | exit code 0；compatible baseline 为 `1497/1497` | [PSRH-043_build_result.md](../tests/evidence/PSRH-043_build_result.md) |
| Application BIN | `1,906,400 B` | baseline `1,904,928 B`；delta `+1,472 B` | [PSRH-043_resource_measurement.md](../tests/evidence/PSRH-043_resource_measurement.md) |
| Flash Code delta | `+1,474 B` | baseline `1,752,036 B`，integrated `1,753,510 B` | 同上 |
| DIRAM / `.bss` delta | `+1,920 B` | `s_task_records=768 B`，`s_task_status=1,152 B` | 同上 |
| Runtime task snapshot | `16/16` | capacity `32`，`truncated=no` | [PSRH-043_hardware_deferral.md](../tests/evidence/PSRH-043_hardware_deferral.md) |
| Controller loops | `2` | 两轮 `0 → 1 → 0`，同一持久 storage | [PSRH-042_controller_change_to_mode_hil_20260809.md](../tests/evidence/PSRH-042_controller_change_to_mode_hil_20260809.md) |
| Physical USB absence | `29 s` | 串口设备消失后重新枚举；不是 RTS/reset | [PSRH-043_hardware_deferral.md](../tests/evidence/PSRH-043_hardware_deferral.md) |
| External manifest coverage | `42 + 8 + 15 = 65` items | 三套 manifest；sha256 均记录 | [PSRH-043 reviewer summary](session-output-20260809-psrh-043-independent-reviewer.md) |

“没有项目自有 compiler warning”只表示该 warning 在 integrated log 中已消失；
项目 CMake 兼容性、ESP-Matter/ConnectedHomeIP 和 Kconfig warnings 仍被保留并
分类，不能写成“零 warning”。详见 [warning classification](../tests/evidence/PSRH-043_warning_classification.md)。

## 6. 测试结果边界

### PSRH-042：按其自身证据集解释

| 用例/门禁 | 结论 | 可以说什么 | 不能扩展成什么 |
|---|---|---|---|
| T05 Matter 子项 | PASS（new Fabric HIL） | Occupancy 记录为 `1 → 1 → 1` | 不能代表所有雷达/网络条件 |
| T16 / Controller `ChangeToMode` | PASS | `CurrentMode 0 → 1 → 0` 及两轮成功路径 | 不能代表所有模式、控制器生态或长时稳定性 |
| NIGHT guard | PASS | 窗口外 `ChangeToMode(2)` 被拒绝，记录了脱敏错误码 | 不能写成自动 NIGHT 进入已完整覆盖 |
| T14 | PARTIAL | operational CASE/读取被保留 | raw PASE/NOC success chain 在该 PSRH-042 证据集未保留 |
| T17 | PARTIAL | 自动退出及退出后 `CurrentMode=0` 被捕获 | 自动进入未单独捕获 |
| T19 | PARTIAL | 离线本地 `NORMAL→QUIET` 被捕获 | AP 恢复后的最新 Matter 值读取未在该证据集验证 |
| 设备重启 | PARTIAL | 普通重启后 CASE 与 EP2 读取被保留 | EP1/EP2 双端点读取未同时保留 |

后续 PSRH-043 的授权精确镜像 HIL 确实记录了 BLE GATT/PASE/NOC/CASE、DHT22、
Wi-Fi 和断电恢复；它是另一任务、另一证据集。求职材料可以引用它的明确事实，
但不能借此抹掉 PSRH-042 自身记录的 PARTIAL 标签。

### PSRH-043：最终集成证据

PSRH-043 的最终任务契约和独立 Reviewer 记录将其请求的集成 HIL 门禁标为
PASS，并绑定：

- reviewed HEAD `4af6c6350e070a3129b63596587d46042cf34148`；
- firmware merge `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`；
- App BIN SHA-256 `3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c`；
- closeout manifest `929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143`；
- BLE manifest `4ef5aa8bd65f3f4dc0ff8f9cc5ccb6e3ccb4957a64c8e0198d631fc4e0b5b6fb`；
- final HIL manifest `1792415f3734a81147d349d384999f0cf454c07ef6a75ea48d4f71e82e5362fb`。

原始日志和镜像在仓库外受控目录；仓库内只保留脱敏摘要、相对路径和哈希。

## 7. 已知限制与“不宣称”边界

以下项目不能写成 PSRH-044 或整个项目已完成：

- **T01/T02/T18**：短时经过、静坐不动和环境告警热源测试在本次收口没有补测；
  原有 DEFERRED/BLOCKED 记录仍有效。
- **T08**：错误 Wi-Fi 密码路径未形成 PASS，保持 Not claimed。
- **T11**：配置损坏恢复未执行；该破坏性注入需要单独批准。
- **T13 watchdog 边界**：只观察到运行窗口内没有 TWDT failure；没有执行受控
  watchdog fault injection，因此不能写成 watchdog reset 测试 PASS。
- **T20**：24 小时稳定性测试未执行；短时资源快照不能替代本项。
- **保留的 PARTIAL**：PSRH-042 T14、T17、T19，以及设备重启后的 EP1/EP2
  配对证据不能写成 PASS。
- **长期/产品能力**：24 小时稳定性、功耗预算、量产一致性、OTA/A-B 回滚、
  第三方 Matter 生态兼容性、生产级安全/隐私认证均没有被本次文档收口证明。
- **物理交付**：没有在本任务中完成洞洞板、外壳、线束、装配、照片、架构图或
  演示视频；开发板加模块不能表述为完成的消费电子产品。
- **安全/用途**：不宣称医疗、生命监测、专业安防、生命安全控制或安全认证。

推荐的安全措辞是“在记录的 ESP32-C6、精确镜像、控制器、工具链和证据边界
下验证了……”。不推荐使用“production-ready”“fully validated”“all tests
passed”“zero warnings”或“product-grade”。

## 8. PSRH-044 验收与 Reviewer 交接

本任务只需要以下检查：

1. 变更文件范围只包含 `agent/tasks/PSRH-044.yml`、两个历史任务状态文件、
   `README.md`、`docs/project-plan.md`、本文件、`docs/resume-materials.md` 和
   `tests/README.md`。
2. 变更文档中的仓库相对链接可解析，Markdown 无空格/尾随空白等格式问题。
3. 对照任务契约、handoff、证据摘要和提交历史，复核数字、状态、时间和限制。
4. 将本任务交给 `gpt-5.6-sol` 做独立文档审查。

### Builder 机械检查结果（2026-08-09）

| 检查 | 结果 |
|---|---|
| YAML 解析（PSRH-042/043/044） | **PASS** |
| 变更文件范围 | **PASS**；仅 8 个 PSRH-044 交付路径 |
| 仓库内相对 Markdown 链接 | **PASS** |
| `git diff --check` | **PASS** |
| 数字、状态、证据和限制交叉核对 | **PASS** |
| firmware、Host 测试实现、已有 evidence、hardware 变更 | **PASS**；无变更 |
| 新构建、flash、HIL、T01/T02/T18 或硬件操作 | **未执行**；按 PSRH-044 明确排除 |

### Independent Reviewer 与 Human Lead 收口（2026-08-09）

- 初审 `aee610d`：`REQUEST_CHANGES`，要求恢复 T01–T20 可执行测试规范，并在
  顶层材料明确披露 T08、T11、T13、T20 边界。
- 修复提交 `ff7e60c`：20 个测试定义完整恢复，四项限制在 README、项目收口、
  求职材料和测试索引中一致披露。
- 独立 Reviewer 最终结论：**PASS**。
- Human Lead：**ACCEPTED**，任务推进到 `READY_TO_MERGE`。

PSRH-044 不需要，也不授权新的 `idf.py`、`ninja`、Host 测试、烧录、串口占用
或 HIL 操作。本结论只覆盖文档和既有证据引用；任何实现、证据、量化陈述或
范围变化都需要重新审查。
