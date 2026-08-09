# Resume and Interview Materials

本文件是 PSRH-044 的求职材料收口版。简历只应使用下面的“可核实表述”，
并保留相应的提交和证据入口；不要把多条证据拼成原始记录没有支持的更强
结论。

## 一、中文简历项目描述

### 完整版

**PrivacySense Matter Room Hub｜ESP32-C6 / ESP-Matter 1.5 / C / C++**

- 设计并实现无摄像头的本地房间状态中枢：以 HLK-LD2410C-P 毫米波雷达和
  DHT22 产生占用、用户模式和环境告警三维状态，通过 BLE commissioning 与
  Matter over Wi-Fi 对外提供语义状态；保持 EP0/EP1/EP2 拓扑和 `OccupancySensing`、
  `ModeSelect` 业务映射。
- 针对 ESP-Matter 1.5 的异步 `ChangeToMode` 边界实现有界请求/应答、超时/取消
  失败处理和 retry-safe 状态同步；在默认生产镜像上完成两轮
  `CurrentMode 0→1→0` Controller 闭环验证。
- 增加固定容量运行时诊断：集成 Host tests **130/130 PASS**，ESP32-C6 集成目标
  **1498/1498 PASS**；相对 fresh compatible baseline 的 App BIN 增量为
  **+1,472 B**，诊断固定 `.bss` 增量为 **+1,920 B**，运行快照保持
  **16/16、capacity 32、`truncated=no`**。

### 精简版（简历空间有限时）

在 ESP32-C6 上构建无摄像头本地房间状态中枢，集成毫米波雷达/DHT22、BLE
commissioning 和 Matter over Wi-Fi；围绕 ESP-Matter 1.5 的异步模式命令实现
有界请求、超时/取消处理和重连同步。既有证据记录 Host **130/130**、集成构建
**1498/1498**、Matter Controller `0→1→0` 两轮闭环，以及 **+1,472 B** App BIN
增量；所有数字均绑定提交和脱敏证据。

### 中文项目关键词

`ESP32-C6` · `ESP-IDF 5.4.1` · `ESP-Matter 1.5` · `FreeRTOS` · `C/C++` ·
`Matter over Wi-Fi` · `BLE commissioning` · `UART/RMT` · `NVS` ·
`状态机` · `故障恢复` · `资源诊断` · `Host test`

## 二、English resume project description

### Full version

**PrivacySense Matter Room Hub | ESP32-C6, ESP-Matter 1.5, C/C++**

- Built a camera-free local room-state hub using an HLK-LD2410C-P mmWave radar and
  DHT22 sensor; modeled occupancy, user mode, and environmental alert as parallel
  state dimensions, with BLE commissioning and Matter over Wi-Fi exposure through
  the recorded EP0/EP1/EP2 topology.
- Hardened the ESP-Matter 1.5 asynchronous `ChangeToMode` boundary with bounded
  request/response handling, timeout/cancellation failure propagation, and
  retry-safe state synchronization; verified two controller-side `CurrentMode
  0→1→0` loops on the recorded production image.
- Added fixed-capacity runtime diagnostics: **130/130 Host tests passed** and the
  integrated ESP32-C6 target completed **1498/1498**; the application BIN delta
  versus the fresh compatible baseline was **+1,472 B**, diagnostic fixed `.bss`
  delta was **+1,920 B**, and accepted runtime snapshots remained **16/16 with
  capacity 32 and `truncated=no`**.

### Short version

Built a camera-free ESP32-C6 room-state hub integrating mmWave/DHT22 sensing, BLE
commissioning, and Matter over Wi-Fi. Hardened the ESP-Matter 1.5 asynchronous mode
command path with bounded failure handling and retry-safe synchronization; evidence
records 130/130 Host tests, a 1498/1498 integrated build, two `0→1→0` controller
loops, and a +1,472 B application-image delta.

### English keywords

`ESP32-C6` · `ESP-IDF 5.4.1` · `ESP-Matter 1.5` · `FreeRTOS` · `C/C++` ·
`Matter over Wi-Fi` · `BLE commissioning` · `UART/RMT` · `NVS` ·
`state machine` · `fault recovery` · `bounded diagnostics` · `Host testing`

## 三、可核实的量化成果清单

| 简历数字/事实 | 可核实来源 | 使用边界 |
|---|---|---|
| Camera-free local room-state hub with LD2410C/DHT22 and three parallel state dimensions | [state model](state-model.md)、[BOM](../hardware/BOM.csv)、[R12 phase-1 acceptance](../tests/evidence/R12_phase1_acceptance_record.md)、[PSRH-043 HIL evidence](../tests/evidence/PSRH-043_hardware_deferral.md) | 可说已实现并在记录的开发板/精确镜像上验证相关路径；不说完成产品装配或全场景覆盖 |
| BLE commissioning / Matter over Wi-Fi transport boundary | [commissioning lifecycle](commissioning-lifecycle.md)、[Matter data model](matter-data-model.md)、[PSRH-043 HIL evidence](../tests/evidence/PSRH-043_hardware_deferral.md) | 只引用记录的授权精确镜像和控制器；不说兼容所有平台或网络 |
| EP0/EP1/EP2，EP2 `NORMAL=0`、`QUIET=1`、`NIGHT=2` | [PSRH-042 task contract](../agent/tasks/PSRH-042.yml)，[PSRH-042 handoff](../tests/evidence/PSRH-042_builder_handoff.md)；implementation `2f5d58379360d759aa4c6ffd8c5574ff247c2cf5` | 只能说记录的 Matter 数据模型保持；不说已覆盖所有生态 UI |
| 两轮 Controller `0→1→0` | [PSRH-042 HIL summary](../tests/evidence/PSRH-042_controller_change_to_mode_hil_20260809.md)，evidence commit `0dda51d4aeef2a1717445dcb96c462094b8ed1ec` | 这是特定生产镜像、持久 storage 和控制器的 success path |
| Host `130/130 PASS` | [PSRH-043 Host result](../tests/evidence/PSRH-043_host_test_result.md)，integrated firmware merge `c2a0ff09d70775a9d582bb3e8a71e455cfb49529` | 不写成覆盖所有 HIL/硬件场景 |
| ESP32-C6 build `1498/1498` | [PSRH-043 build result](../tests/evidence/PSRH-043_build_result.md)，integrated evidence `0d403f13ec0a0e4b2c32e16f35893f987606ae1d` | 是已记录工具链和构建目录下的 integrated build |
| App BIN `+1,472 B` | [resource measurement](../tests/evidence/PSRH-043_resource_measurement.md) | 相对 fresh compatible baseline；不能泛化为所有配置/编译选项 |
| Diagnostic `.bss` `+1,920 B` | 同上；ELF symbols `768 B + 1,152 B` | 是固定诊断存储的静态增量，不是“总 RAM 只增加 1,920 B” |
| Runtime `16/16`, capacity `32`, `truncated=no` | [PSRH-043 HIL evidence](../tests/evidence/PSRH-043_hardware_deferral.md) | 是记录的运行快照，不是 24 小时稳定性证明 |
| Physical USB absence `29 s` | 同上 | 是一次受控真实断电观察，不是电源安全/量产认证 |
| PSRH-042/043 current status `DONE` | [PSRH-042.yml](../agent/tasks/PSRH-042.yml)、[PSRH-043.yml](../agent/tasks/PSRH-043.yml) | DONE 是任务治理状态，不等同于所有历史测试均 PASS |

## 四、面试讲解提纲

### 30 秒版本

这是一个基于 ESP32-C6 的无摄像头房间状态中枢。雷达和 DHT22 只在本地计算
占用、模式和环境告警，BLE 用于 Matter 配网，Matter over Wi-Fi 用于语义状态
读取和模式切换。我重点解决了 ESP-Matter 1.5 中异步 `ChangeToMode` 与本地
状态机之间的边界问题，并用 Host、集成构建、资源快照和精确镜像 HIL 留下了
可追溯证据。项目是工程原型，不宣称量产产品。

### 2 分钟结构

1. **问题与取舍**：房间状态感知不依赖摄像头；原始传感器数据不上传，断网时
   保留本地状态机和 UI 行为。
2. **状态建模**：占用、用户模式、环境告警并行维护，避免一个维度覆盖另一个
   维度；Matter 只暴露批准的业务属性。
3. **任务边界**：传感器、网络、按键、状态机和 Matter adapter 通过事件/队列
   交互；Matter 回调不直接跨任务携带临时上下文。
4. **最难问题**：`ChangeToMode` 不能在请求上下文失效后再异步使用；因此用有界
   请求/应答等待真实状态机结果，把 queue/snapshot/timeout/update 失败映射为
   命令失败，再允许 `CurrentMode::Set()`。
5. **可靠性与资源**：PSRH-043 增加固定容量诊断，记录任务数量、捕获数量、
   capacity、truncation、heap、reset reason 和 task HWM，并把 warning 分类而
   不是静默抑制。
6. **验证方法**：先看 Host 130/130 和集成 build 1498/1498，再看精确 BIN、
   资源快照、Matter 读写、BLE/Wi-Fi/DHT22/断电恢复证据；最后明确 PARTIAL 和
   未执行测试，而不是用“全部通过”概括。

### 5 分钟深挖顺序

#### A. 为什么不用摄像头？

目标是展示低打扰房间状态感知。系统只输出 `VACANT/OCCUPIED/UNKNOWN`、模式
和告警等语义状态，不保存身份信息；这同时降低了数据处理范围和隐私风险。

#### B. 为什么 Matter 只做两个业务 Cluster？

项目冻结了 EP0/EP1/EP2，并选择 `OccupancySensing` 和 `ModeSelect`。环境告警
首版只在本地 UI 展示，不为了“看起来功能更多”而创建不存在的 Endpoint 或
`stateValue` 属性。

#### C. 异步命令的失败路径怎么处理？

Controller 命令进入有界请求/应答路径；状态机在有限等待内确认队列、快照、
夜间窗口策略和本地转换结果。任何 queue、timeout、snapshot 或 update 失败
都在 `CurrentMode::Set()` 前返回失败。报告失败时保留 generation-based
同步请求，避免一次失败丢掉后续状态。

#### D. 资源数字怎么证明？

Host/build 数字来自提交后的 Markdown 结果和外部日志哈希；静态增量来自 fresh
compatible baseline 与 integrated size 对比；动态诊断来自精确 App BIN 的
运行快照。三种证据分别回答“能编译”“静态增加多少”“运行时是否捕获完整”，
不能互相替代。

#### E. 还有哪些没有完成？

T01、T02、T18 没有在本次收口补测；T08 错误 Wi-Fi 密码路径未形成 PASS，T11
配置损坏恢复未执行，T20 24 小时稳定性测试未执行；T13 只有运行窗口
观察，没有执行 watchdog fault injection。PSRH-042 的 T14/T17/T19 和设备
重启端点配对仍按 PARTIAL 记录；没有在本任务做物理装配、视觉材料、24 小时
稳定性或产品认证。这些限制是项目可信度的一部分，应主动说明。

## 五、常见追问的安全回答

| 追问 | 建议回答 |
|---|---|
| “是不是所有测试都通过了？” | 不是。PSRH-043 请求的集成 HIL 门禁有 PASS 记录，但 PSRH-042 保留了多个 PARTIAL；T01/T02/T08/T11/T18/T20 没有在本次收口形成 PASS，T13 也没有执行 watchdog fault injection。 |
| “是不是已经量产？” | 不是。这是开发板和模块组成的可验证原型；没有在本任务完成洞洞板、外壳、装配、量产一致性或认证。 |
| “为什么说零 warning？” | 不这么说。PSRH-043 消除了一个项目自有 unused-function warning；其余项目 CMake、ESP-Matter/ConnectedHomeIP 和 Kconfig warnings 被保留并分类。 |
| “Matter 是否兼容所有平台？” | 不能这样宣称。记录的是锁定 ESP-Matter 1.5、特定控制器、精确镜像和已保存 HIL 证据。 |
| “是否做了 24 小时稳定性？” | 本次没有执行，也不以短时资源快照替代 24 小时稳定性结论。 |
| “能否用于医疗或安防？” | 不能。项目明确不宣称医疗、生命监测、专业安防或生命安全能力。 |

## 六、统一写作规则

推荐：

> “在记录的 ESP32-C6、ESP-Matter 1.5 工具链、精确 App BIN 和受控证据边界下，
> 验证了……；剩余限制为……。”

避免：

- “all tests passed” / “fully validated” / “zero warnings”；
- “production-ready” / “product-grade” / “mass-production”；
- “24-hour stable” / “secure” / “privacy certified”；
- 把“实现了测试准备”写成“测试通过”；
- 把一次授权 HIL 结果写成所有硬件、网络、控制器或生态的普遍能力。
