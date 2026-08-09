# 项目计划书

## 1. 项目名称

`PrivacySense Matter Room Hub`

## 2. 项目背景和目标

许多智能家居设备依赖摄像头、手机 App 或云端服务。卧室、书桌和租住房间需要一种低打扰的状态感知方式：设备只判断房间状态，不采集图像，不上传原始传感器数据，并且断网时仍可本地反馈。

本项目构建一个可演示的消费电子原型，完成：

```text
毫米波/环境传感器 -> 本地状态机 -> RGB/OLED/按键
                              -> BLE 首次配网
                              -> Matter over Wi-Fi
                              -> 家居自动化联动
```

目标是展示硬件集成、实时任务、状态建模、协议接入、异常处理和隐私意识，不宣称量产级智能家居平台能力。

## 3. 典型使用场景

- 回到房间：检测到持续有人，状态为 `OCCUPIED + NORMAL`，可由 Matter 自动打开桌灯。
- 学习或办公：用户按键进入 `QUIET`，RGB 灯低亮显示，自动化可暂停普通提醒。
- 夜间活动：进入配置的夜间时段（默认 22:00–07:00）且检测到有人，模式切换为 `NIGHT`，只点亮低亮度引导灯。
- 离开节能：持续无人超过配置时间，占用状态变为 `VACANT`，本地进入低功耗策略并上报状态。
- 环境提醒：有人且 DHT22 温湿度持续超阈值，环境告警变为 `ALERT`，提示通风或调整环境；CO2 仅在后续安装 SCD40 后参与。

> 以上状态采用并行模型：占用（VACANT/OCCUPIED）、用户模式（NORMAL/QUIET/NIGHT）和环境告警（OK/ALERT）独立计算，不互相覆盖。详见 [状态模型](state-model.md)。
>
> **night 判定方式说明**：现有 LM393 光敏电阻模块只能提供可调阈值的相对明暗信号，不能提供标定 lux。为尽快完成首版，`NIGHT` 仍采用 SNTP 时段判定；光敏模块保留为后续 GPIO10 扩展，不阻塞代码。

## 4. 分阶段计划

预计总周期 3 到 5 周，按每天 2 到 3 小时投入估算；实际进度以硬件到货和 Matter 调试情况为准。

### 阶段 0：需求冻结与环境就绪，0.5 到 1 天

- 按 [开发环境搭建流程](development-environment.md) 复核已安装的 Ubuntu 24.04 WSL2、ESP-IDF v5.4.1 和 ESP-Matter。
- 冻结主控、雷达、传感器和电源方案。
- 冻结首版只使用 Matter over Wi-Fi。
- 输出 GPIO、UART、I2C 和供电连接表：[连接表](../hardware/connection-table.md)。
- 输出冻结 BOM：[BOM.csv](../hardware/BOM.csv)。
- 冻结状态模型和转换表：[状态模型](state-model.md)。
- 冻结 Matter 数据模型：[Matter 数据模型](matter-data-model.md)。
- 冻结任务架构：[任务架构](task-architecture.md)。
- 明确不接市电、不使用摄像头、不上传原始数据。

**开始写代码门槛（已满足）**：工具链已安装；官方 ESP32-C6 Matter 示例存在有效构建产物；开发板可通过 `/dev/ttyUSB0` 识别；实机 Flash 已确认 16 MB；连接表、BOM、状态模型、Matter 数据模型和任务架构已冻结。

**阶段 0 原始门槛的当前解释**：早期计划要求在干净 ESP-IDF shell 中复核
`idf.py size`、记录上游 commit，并完成官方示例烧录和 commissioning。后续
PSRH-042/043 已针对锁定工具链完成集成构建、尺寸、Host 与精确镜像 HIL
证据；这些后续证据是当前项目状态的依据，不把早期缺口改写成当时已经完成。
工具链与证据索引见 [测试与证据索引](../tests/README.md) 和
[项目收口说明](project-closeout.md)。

### 阶段 1：硬件单元验证，2 到 3 天

- 验证 ESP32-C6 供电和串口日志。
- 验证 LD2410C UART 通信和雷达配置保存。
- 验证 AM2302/DHT22 GPIO2 单总线采集、checksum、负温度处理、5 s 周期和连续失败恢复。
- 验证 SSD1306 OLED 的 7-bit I2C 地址、板载上拉和显示刷新。
- 验证按键消抖和 RGB 状态灯；光敏模块首版不接入。

完成条件：每个模块都有独立测试记录和异常现象记录。

> **实际进度（2026-07-20）**：阶段一全部模块已硬件验证并条件验收通过：LD2410C UART 通信及配置保存（R11）、DHT22、OLED、按键模式转换、RGB 状态灯（见 R12_phase1_acceptance_record.md）。电气测量因缺万用表延期。

### 阶段 2：本地状态机，3 到 5 天

- 按 [状态模型](state-model.md) 实现并行三维度状态机：占用状态、用户模式、环境告警。
- 实现阈值、去抖、退出延迟和传感器失效策略。
- 处理传感器断线、数据超时和异常值。
- 确保无 Wi-Fi 时仍能正确显示本地状态。

完成条件：连续有人、连续无人、短时经过、夜间活动和雷达断线均有记录。

> **实际进度（2026-07-20）**：阶段二软件门禁通过，硬件验证有条件通过，尚未全量关闭。
>
> - Phase 2 software/build: **PASS**
> - Phase 2 local hardware core paths: **PASS**
> - Overall hardware validation: **CONDITIONAL PASS**
> - Evidence: `monitor_phase2_20260720_210630.log:338`
>
> | 测试 | Reviewer 结论 |
> |---|---|
> | T03、T04 | PASS，日志有 OCCUPIED→VACANT→OCCUPIED 证据 |
> | T05 | 本地检测 PASS；Matter 保留最后有效值尚未验证，完整用例 PARTIAL |
> | T06 | PASS，雷达恢复后 UNKNOWN→OCCUPIED |
> | T07 | PASS，连续三次失败后离线，下一有效帧恢复，期间无重启 |
> | T16 | 本地按键行为按人工观察接受；日志无 BUTTON: mode...，Matter CurrentMode 因 STUB 未验证，正式状态 PARTIAL |
> | T01、T02 | DEFERRED，雷达行为重要用例，保留为开放项 |
> | T17 | 合理延至阶段三 |
> | T18 | BLOCKED，不等于失败；缺可控热源，保持开放 |
>
> - Open: T01, T02, T18
> - Deferred to Phase 3: T17（SNTP/NIGHT 自动切换）
> - Deferred to Phase 4: T05 Matter 子项（occupancy 保留最后有效值）、T16 Matter 子项（CurrentMode 读取）
> - Known limitation: 不支持 OLED 带电插拔

### 阶段 3：Wi-Fi 与 BLE commissioning，3 到 5 天

- 按 [配网生命周期](commissioning-lifecycle.md) 完成 BLE 首次配网。
- 保存并校验设备配置，处理凭据写入失败。
- 实现 Wi-Fi 断开重连、超时、退避和错误密码检测。
- 清理 commissioning 完成后的 BLE 资源。
- 实现恢复出厂触发与防误触。
- 实现配置版本迁移。
- **补充验证 T17（SNTP/NIGHT 自动切换）**：Wi-Fi 连接后 SNTP 同步时间，验证夜间时段自动切换 NIGHT 模式及退出恢复。

完成条件：首次配网、重复配网、错误密码、路由器重启和设备重启均可复现并记录；SNTP/NIGHT 自动切换验证通过。

> **阶段 3 实施进度（8 步开发顺序）**：
> - Step 1 `ebcc38e`: network_reconnect_sm 状态机重构 + host 测试 ✅
> - Step 2 `7c95a09`: 最小 BLE commissioning 闭环 + Reviewer 反馈修复 ✅
> - Step 3 `372c759`: Matter 生命周期回调 → app_event_t 派发，wifi_connected/matter_commissioned 追踪 ✅
> - Step 4 `613b9a7`: 长按工厂复位 + 防误触倒计时 + 绿/红 LED 反馈 ✅
> - Step 5 `f08b40c`: BLE 资源释放、fabric 计数（FabricTable）、commissioning window 打开跟踪 ✅
> - Step 6 `5aae27a`: EP1/EP2 端点创建 + Matter 属性上报 + ChangeToMode 命令处理 ✅
> - Step 7: 文档更新与收尾 ✅
> - 历史风险已由 PSRH-043 的授权精确镜像 HIL 补充证据覆盖：BLE GATT/PASE/NOC/CASE、Wi-Fi 配置和 `CommissioningComplete errorCode=0` 均有脱敏记录。该结论只适用于记录的镜像、控制器和证据边界，不等同于量产覆盖。
> - 建时间：2026-07-21 → 2026-07-22

> **阶段 2 遗留项归属说明**：T05（雷达断线）和 T16（按键模式切换）的 Matter 子项（occupancy 保留最后有效值、CurrentMode 读取）属于 Matter 数据模型范畴，移至阶段 4 实现和验证，不纳入阶段 3 范围。

### 阶段 4：Matter over Wi-Fi，4 到 7 天

- 按 [Matter 数据模型](matter-data-model.md) 实现 3 个 Endpoint（含 Endpoint 0）和 2 个业务 Cluster：OccupancySensing 与 ModeSelect。
- 完成设备 commissioning 和控制器发现。
- 将占用状态和用户模式映射到独立 Matter 属性；环境告警首版只在本地 RGB/OLED 显示，不创建虚假的 Endpoint 3。
- 实现断网期间本地状态保持和重连后属性同步。
- 验证控制器重启、设备离线、重新上线和状态同步。
- **补充验证 T05 Matter 子项**：雷达断线后 Matter `occupancy` 保留上一次有效值；雷达恢复后属性同步更新。
- **补充验证 T16 Matter 子项**：按键切换模式后 Matter `CurrentMode` 属性同步更新。

完成条件：至少使用一个实际 Matter 控制器完成配网、状态读取和自动化联动，并保存演示记录。

> **阶段 3 提前完成项**：EP1/EP2 端点和属性上报已在阶段 3 Step 6 实现（commit `5aae27a`）；后续 PSRH-042/043 继续完成了锁定 SDK 兼容、控制器读取/写入和精确镜像 commissioning 证据。当前证据仍按测试用例边界分别记录，不能把一次 HIL 读写扩展成所有场景或生态兼容性结论。

### 阶段 5：可靠性与资源检查，3 到 5 天

- 按 [任务架构](task-architecture.md) 核验任务栈余量、堆使用和 watchdog。
- 验证 watchdog、异常日志和复位原因。
- 检查网络抖动、传感器拔插和配置损坏。
- 按 [OTA 安全边界](ota-safety.md) 完成设计评审或最小可验证实现，不夸大为产品级 A/B 回滚。
- 按 [测试矩阵](../tests/README.md) 执行测试用例并保存证据。

完成条件：形成异常场景测试表、资源使用表和已知问题清单。

> **阶段 5 实际收口（2026-08-09）**：PSRH-043 已完成集成构建、Host 测试、资源诊断、告警分类和授权 HIL 收口；独立 Reviewer PASS，Human Lead 接受，随后由 PSRH-044 将任务记录从 `READY_TO_MERGE` 更新为 `DONE`。具体数字和边界见 [项目收口说明](project-closeout.md) 与 [测试索引](../tests/README.md)。

### 阶段 6：文档与职业材料收口，文档-only

Human Lead 在 2026-08-09 对原阶段 6 做出范围调整：本阶段只收口可审计的
项目叙述、测试索引和求职材料，不把物理装配或展示材料作为本轮完成条件。

**本阶段包含：**

- 更新根目录 README，使当前集成状态、证据入口、边界和非宣称事项一致。
- 在本计划书中记录范围调整和 Human Lead 决定。
- 创建最终项目收口说明，绑定 PSRH-042/043 的提交、证据和量化结果。
- 整理 `tests/README.md`，区分 PASS、PARTIAL、DEFERRED、BLOCKED、历史证据和本轮未执行项。
- 创建中文/英文简历项目描述、面试讲解提纲和可核实的量化成果清单。
- 将 PSRH-042、PSRH-043 的治理状态从 `READY_TO_MERGE` 更新为 `DONE`；不改写历史测试证据的时间语境。

**本阶段明确排除：**

- 固件或测试实现修改；重新构建、烧录或 HIL。
- T01、T02、T18 补测。
- 洞洞板、外壳、线束、装配和传感器安装整理。
- 架构图、演示视频、照片或其他视觉展示材料。

**完成条件：**变更仅落在 PSRH-044 任务契约及其文档交付路径；仓库内相对
链接、Markdown 格式和事实引用检查通过；每个求职陈述都能回指已有提交或
证据文件；已知限制和“不宣称”边界明确；最后交由独立 Reviewer 审查。

> **阶段 6 实际收口（2026-08-09）**：`ff7e60c` 已恢复完整 T01–T20 测试规范并
> 补齐未执行门禁披露；独立 Reviewer PASS，Human Lead 明确接受。PSRH-044
> 当前状态为 `READY_TO_MERGE`，本轮没有执行构建、烧录、HIL 或硬件操作。

## 5. 主要风险与应对

| 风险 | 应对 |
|---|---|
| 毫米波误触发 | 持续时间、退出延迟和多次采样，不能把单次采样直接当成状态变化 |
| Wi-Fi 断开 | 状态识别和灯光反馈与网络任务解耦 |
| Matter 调试耗时 | 首版只做一个节点、一个控制器和两个业务 Endpoint，先完成 Wi-Fi 版本 |
| 控制器 UI 不展示 ModeSelect | 先用 `chip-tool` 验证 Cluster/命令，再把目标生态是否展示该模式作为兼容性结果记录，不用伪造其他设备类型 |
| 模块电平不匹配 | 采购前核对数据手册，必要时加入电平转换和独立供电 |
| DHT22 时序受网络任务影响 | 使用 RMT 捕获脉宽、5 s 周期和 checksum；不直接移植 STM32 忙等/长临界区代码 |
| ESP-IDF 与 Pigweed Python 环境混用 | 固件构建使用干净 ESP-IDF shell；`chip-tool`/Pigweed 使用独立终端，不在同一 shell 叠加虚拟环境 |
| PCB 经验不足 | 先用开发板和模块完成闭环，再委托设计低复杂度载板 |
| 隐私风险 | 只存储语义状态和统计数据，不记录原始雷达数据和个人身份信息 |

## 6. 最终交付物

项目已有的实现、硬件设计资料和脱敏测试证据继续由各自目录维护。本次
PSRH-044 的收口交付物是：

- [根 README](../README.md)：当前项目定位、状态、验证摘要和边界。
- [项目计划书](project-plan.md)：Phase 6 范围调整与 Human Lead 决策记录。
- [项目收口说明](project-closeout.md)：提交/证据追溯、量化结果、限制和不宣称边界。
- [简历与面试材料](resume-materials.md)：中英文项目描述、面试提纲和量化成果。
- [测试与证据索引](../tests/README.md)：测试状态和证据入口。

本轮不把洞洞板、外壳、装配照片、架构图或演示视频列为已完成交付物。
