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

**阶段 0 最终验收仍需补齐**：在干净 ESP-IDF shell 中复核 `idf.py size`，记录三个上游仓库 commit，并完成一次官方示例烧录和 commissioning。这些项目不阻塞 Builder AI 开始驱动和本地状态机代码，但必须在阶段 3 前完成。

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

### 阶段 5：可靠性与资源检查，3 到 5 天

- 按 [任务架构](task-architecture.md) 核验任务栈余量、堆使用和 watchdog。
- 验证 watchdog、异常日志和复位原因。
- 检查网络抖动、传感器拔插和配置损坏。
- 按 [OTA 安全边界](ota-safety.md) 完成设计评审或最小可验证实现，不夸大为产品级 A/B 回滚。
- 按 [测试矩阵](../tests/README.md) 执行测试用例并保存证据。

完成条件：形成异常场景测试表、资源使用表和已知问题清单。

### 阶段 6：样机和简历材料，2 到 3 天

- 将面包板整理为洞洞板或模块化载板。
- 完成外壳和传感器安装方向记录。
- 绘制系统架构图、状态转换图和演示视频。
- 整理 README、测试证据和简历项目描述。

完成条件：能够解释硬件连接、任务划分、状态机、Matter 数据映射和异常处理。

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

- 可运行的 ESP32-C6 原型设备。
- 硬件 BOM、连接表和装配照片。
- BLE 配网、Matter 联动和异常测试记录。
- 状态机说明、系统架构图和 README。
- 一份与实际代码和证据一致、面试可讲解的项目描述。
