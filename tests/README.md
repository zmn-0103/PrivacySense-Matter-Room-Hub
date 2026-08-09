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

## 8. 原始 T01–T20 可执行测试规范（保留）

本节保留基线 `e1db746` 中的测试规范。它定义每个用例的前置条件、执行步骤、
期望结果和证据要求，不代表 PSRH-044 重新执行了这些测试。用例的当前状态
以本页第 3 节的“测试矩阵与边界”为准；规范和结果是两个不同层次的记录。

### 8.1 证据文件格式

每个测试结果存放在 `tests/evidence/` 目录下，建议命名为：

```text
tests/evidence/<test_id>_<version>_<date>.md
```

每个证据文件应包含：

- 测试 ID 和标题；
- 固件版本号；
- 硬件版本号（开发板版本、模块批次）；
- 前置条件、执行步骤、期望状态和实际结果；
- 结论（PASS / FAIL / BLOCKED）；
- 脱敏串口日志摘要（移除 Wi-Fi 密码、Matter payload）；
- 可选的照片/视频引用（文件存放在本地，不提交二进制文件）。

### T01: 毫米波误触发 — 短时经过

| 项 | 内容 |
|---|---|
| 前置条件 | 设备正常运行，占用状态为 VACANT，房间无人 |
| 步骤 | 1. 人员从雷达前方快速经过（< 2 s）<br>2. 观察占用状态和 RGB |
| 期望状态 | 占用状态保持 VACANT（ENTRY_CONFIRM_MS 去抖），RGB 不变为绿色 |
| 证据 | 串口日志（occupancy 状态变化记录） |
| 当前状态 | **DEFERRED**：需人员在雷达前精确控制经过时间 < 2s，当前无法稳定复现，待有助手配合或搭建测试台后补充 |

### T02: 毫米波误触发 — 静坐不动

| 项 | 内容 |
|---|---|
| 前置条件 | 设备正常运行，占用状态为 OCCUPIED |
| 步骤 | 1. 人员静坐不动 3 min<br>2. 观察占用状态 |
| 期望状态 | 占用状态保持 OCCUPIED（EXIT_DELAY_MS 内不因静止判定无人） |
| 证据 | 串口日志 |
| 当前状态 | **DEFERRED**：需人员在雷达前静坐 3 min 且不被中断，当前无法稳定复现，待有助手配合或搭建测试台后补充 |

### T03: 持续有人 → 离开

| 项 | 内容 |
|---|---|
| 前置条件 | 占用状态为 OCCUPIED，人员在房间内 |
| 步骤 | 1. 人员离开房间<br>2. 等待 EXIT_DELAY_MS + 10 s<br>3. 观察占用状态和 RGB |
| 期望状态 | 离开后 120 s（默认）占用状态变为 VACANT，RGB 熄灭 |
| 证据 | 串口日志（含时间戳） |

### T04: 持续无人 → 进入

| 项 | 内容 |
|---|---|
| 前置条件 | 占用状态为 VACANT，房间无人 |
| 步骤 | 1. 人员进入房间并停留<br>2. 等待 ENTRY_CONFIRM_MS + 2 s<br>3. 观察占用状态和 RGB |
| 期望状态 | 进入后 2 s（默认）占用状态变为 OCCUPIED，RGB 绿色 |
| 证据 | 串口日志 |

### T05: 雷达断线检测

| 项 | 内容 |
|---|---|
| 前置条件 | 设备正常运行，雷达通信正常 |
| 步骤 | 1. 拔掉 LD2410C UART 连线<br>2. 等待 SENSOR_TIMEOUT_MS + 5 s<br>3. 观察 RGB 和 Matter occupancy 属性 |
| 期望状态 | RGB 红色慢闪 (1 Hz)，occupancy 保留上一次有效值 |
| 证据 | 串口日志 + RGB 照片/视频 |

### T06: 雷达恢复

| 项 | 内容 |
|---|---|
| 前置条件 | 雷达断线，占用状态为 UNKNOWN |
| 步骤 | 1. 重新连接 LD2410C UART<br>2. 等待 5 s<br>3. 观察占用状态 |
| 期望状态 | 占用状态根据雷达数据恢复（target_present → ENTRY_CONFIRM_MS 后 OCCUPIED，无目标 → EXIT_DELAY_MS 后 VACANT），RGB 恢复正常 |
| 证据 | 串口日志 |

### T07: DHT22 传感器异常

| 项 | 内容 |
|---|---|
| 前置条件 | 设备正常运行，DHT22 每 5 s 产生有效温湿度数据 |
| 步骤 | 1. 断开 DHT22 DATA 或注入 checksum 错误<br>2. 等待 15 s（连续 3 次读取失败）<br>3. 观察错误计数、`env_sensor_online`、RGB/OLED 和本地环境告警 |
| 期望状态 | 单次失败不改变在线状态；连续 3 次失败后标记离线，RGB 附加黄色短闪，环境告警冻结；不得重启设备；恢复有效帧后重新开始告警确认计时 |
| 证据 | 串口日志 |

### T08: 错误 Wi-Fi 密码

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已配网，Wi-Fi 正常连接 |
| 步骤 | 1. 通过 Matter 或恢复出厂重新配网，输入错误密码<br>2. 观察连接行为 |
| 期望状态 | 重试 3 次（1 s / 2 s / 4 s 退避），全部失败后 RGB 红色快闪 (2 Hz)，等待用户重新配网 |
| 证据 | 串口日志（含 WIFI_REASON 代码） |
| 当前状态 | **Not claimed**：本项目收口没有形成该路径的 PASS 证据 |

### T09: 路由器重启

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已连接 Wi-Fi，正常运行 |
| 步骤 | 1. 关闭路由器<br>2. 等待 30 s<br>3. 开启路由器<br>4. 等待 60 s<br>5. 观察 Wi-Fi 连接和 Matter 属性同步 |
| 期望状态 | 路由器关闭后 Wi-Fi 断开，RGB 白色慢闪；路由器恢复后自动重连，Matter 属性强制上报 |
| 证据 | 串口日志（含重连时间戳） |

### T10: Matter 控制器重启

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已 commissioned，控制器在线 |
| 步骤 | 1. 重启 Matter 控制器<br>2. 等待控制器恢复<br>3. 读取设备属性 |
| 期望状态 | 控制器恢复后能正确读取当前 `occupancy`、`CurrentMode`；不得读取或上报首版不存在的 `stateValue` |
| 证据 | 控制器截图 + 串口日志 |

### T11: 配置损坏恢复

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已配网并运行 |
| 步骤 | 1. 通过调试接口损坏 NVS config 命名空间<br>2. 重启设备 |
| 期望状态 | 设备检测到配置损坏，加载默认配置，不丢失 Wi-Fi 凭据（独立命名空间），正常运行 |
| 证据 | 串口日志（含 config_corrupted 标记） |
| 当前状态 | **Not claimed**：该配置损坏注入未执行；需要单独批准的破坏性测试 |

### T12: 断电重启

| 项 | 内容 |
|---|---|
| 前置条件 | 设备正常运行，占用状态为 OCCUPIED |
| 步骤 | 1. 拔掉 USB 电源<br>2. 等待 5 s<br>3. 重新接通电源<br>4. 观察启动过程 |
| 期望状态 | 设备重启，从 NVS 恢复配置，状态机初始化为 VACANT + NORMAL + OK，Wi-Fi 自动连接，Matter 自动恢复 |
| 证据 | 串口日志（完整启动日志） |

### T13: Watchdog 复位

| 项 | 内容 |
|---|---|
| 前置条件 | 设备正常运行 |
| 步骤 | 1. 通过受控调试注入阻塞某个已注册任务（模拟死锁）<br>2. 等待 TWDT 超时（10 s）<br>3. 观察系统行为；测试完成后移除调试注入 |
| 期望状态 | TWDT 超时 → 打印任务列表 → 系统重启 → 启动日志标记 reset_reason = TWDT |
| 证据 | 串口日志（含 TWDT 超时信息和任务列表） |
| 当前状态 | **Observation only**：运行窗口没有观察到 TWDT failure；未执行 watchdog fault injection，不能写成 TWDT reset PASS |

### T14: 首次 BLE 配网

| 项 | 内容 |
|---|---|
| 前置条件 | 设备恢复出厂状态，NVS 无凭据 |
| 步骤 | 1. 设备上电<br>2. 使用 Matter 控制器扫描并配网<br>3. 输入 Wi-Fi 凭据<br>4. 等待配网完成 |
| 期望状态 | BLE 蓝色快闪 → Wi-Fi 连接成功 → Matter commissioned → RGB 恢复正常 |
| 证据 | 串口日志 + 控制器截图 |

### T15: 重新配网

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已配网并正常运行 |
| 步骤 | 1. 长按按键 5 s（RGB 红色倒计时）<br>2. 松开按键<br>3. 等待设备重启进入配网模式<br>4. 使用控制器重新配网 |
| 期望状态 | 设备擦除旧凭据 → 重启 → 进入 BLE 配网模式 → 重新配网成功 |
| 证据 | 串口日志 + 视频 |

### T16: 模式切换 — 按键

| 项 | 内容 |
|---|---|
| 前置条件 | 占用状态 OCCUPIED，模式 NORMAL |
| 步骤 | 1. 短按按键<br>2. 观察 RGB 和 Matter `CurrentMode`<br>3. 再次短按按键<br>4. 观察 RGB 和 Matter `CurrentMode` |
| 期望状态 | 第一次短按 → QUIET，RGB 蓝色低亮；第二次短按 → NORMAL，RGB 绿色 |
| 证据 | 串口日志 + Matter 属性读取 |

### T17: 夜间模式自动切换

| 项 | 内容 |
|---|---|
| 前置条件 | 当前时间接近 NIGHT_START（22:00），模式 NORMAL，SNTP 已同步 |
| 步骤 | 1. 等待到 22:00<br>2. 观察模式变化<br>3. 等待到 07:00<br>4. 观察模式变化 |
| 期望状态 | 22:00 → NIGHT，RGB 暖白低亮；07:00 → 恢复 NORMAL，RGB 绿色 |
| 证据 | 串口日志（含时间戳） |
| 当前状态 | **DEFERRED/PARTIAL**：自动退出被记录，自动进入未单独捕获；PSRH-044 不补测 |

### T18: 环境告警触发与清除

| 项 | 内容 |
|---|---|
| 前置条件 | 占用状态 OCCUPIED，环境正常 |
| 步骤 | 1. 将 DHT22 与 ESP32-C6/OLED/雷达热源隔离后，用可控热源使读数 > 32 °C<br>2. 等待 60 s（确认时间）<br>3. 观察 RGB/OLED 和本地 `env_alert`<br>4. 移除热源，等待温度 < 30 °C<br>5. 等待 120 s（清除时间）<br>6. 观察本地状态 |
| 期望状态 | 加热 60 s 后 → ALERT，RGB 黄色；冷却 120 s 后 → OK，RGB 恢复；Matter 端不出现环境 `stateValue` Endpoint |
| 证据 | 串口日志（含温度和告警状态） |
| 当前状态 | **BLOCKED**：缺可控高低温源；PSRH-044 明确不补测 |

### T19: 断网期间本地状态保持

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已配网，正常运行 |
| 步骤 | 1. 关闭路由器<br>2. 在房间内进出，切换模式<br>3. 观察 RGB 和本地状态<br>4. 开启路由器<br>5. 验证 Matter 属性同步 |
| 期望状态 | 断网期间状态机正常运行，RGB 正确显示（附加白色慢闪）；重连后 Matter 属性同步到最新值 |
| 证据 | 串口日志 + 视频 |
| 当前状态 | **PARTIAL**：离线本地 `NORMAL→QUIET` 被捕获；PSRH-042 证据集未验证恢复后的最新 Matter 值读取 |

### T20: 长时间运行稳定性

| 项 | 内容 |
|---|---|
| 前置条件 | 设备已配网，正常运行 |
| 步骤 | 1. 连续运行 24 h<br>2. 每小时检查一次状态<br>3. 检查任务栈余量、堆使用、复位次数 |
| 期望状态 | 无异常重启，栈余量 > 20%，堆空闲 > 30%，状态机无卡死 |
| 证据 | 串口日志摘要 + `uxTaskGetStackHighWaterMark()` 记录 |
| 当前状态 | **Not run in closeout**：未执行 24 小时稳定性测试；短时资源快照不替代本项 |

### 8.2 测试执行优先级（历史计划）

| 优先级 | 测试 ID | 说明 |
|---|---|---|
| P0（阻塞发布） | T03, T04, T05, T08, T12, T14 | 核心功能和关键异常恢复 |
| P1（重要） | T01, T02, T06, T07, T09, T10, T15, T19 | 误触发、传感器恢复、网络恢复、配网 |
| P2（建议） | T11, T13, T16, T17, T18 | 配置损坏、watchdog、模式切换、告警 |
| P3（长期） | T20 | 稳定性 |

本节恢复的是测试规范和执行要求，不新增任何测试证据；当前结论、未执行门禁
和跨任务证据边界仍以第 3 节及各任务证据文件为准。
