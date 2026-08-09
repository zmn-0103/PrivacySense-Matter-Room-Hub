# PrivacySense Matter Room Hub

PrivacySense Matter Room Hub 是一个基于 ESP32-C6 的无摄像头房间状态原型：
毫米波雷达和 DHT22 在本地产生语义状态，设备通过 BLE 完成 Matter 配网，
再通过 Matter over Wi-Fi 提供受控的房间状态读取与模式切换。

本仓库同时是一个可审计的学习/求职作品。下面的“已完成”只引用仓库内的
提交、脱敏证据或仓库外受控 artifact manifest；它不等同于量产认证或完整
产品交付。

## 当前状态（2026-08-09）

| 项目 | 当前状态 | 依据与边界 |
|---|---|---|
| PSRH-042 Matter API compatibility | **DONE** | 独立 Reviewer PASS；Human Lead 接受 T14/T17/T19 和设备重启 EP1/EP2 配对证据的 PARTIAL 边界，见 [任务契约](agent/tasks/PSRH-042.yml) 和 [handoff](tests/evidence/PSRH-042_builder_handoff.md)。 |
| PSRH-043 reliability/resource closure | **DONE** | 独立 Reviewer PASS；Human Lead 接受集成构建、资源和授权 HIL 收口，见 [任务契约](agent/tasks/PSRH-043.yml) 和 [handoff](tests/evidence/PSRH-043_builder_handoff.md)。 |
| PSRH-044 documentation/career closeout | **VERIFYING** | 文档-only 分支；等待独立 Reviewer 审查，见 [任务契约](agent/tasks/PSRH-044.yml)。 |

PSRH-044 从指定基线 [`e1db746`](https://github.com/zmn-0103/PrivacySense-Matter-Room-Hub/commit/e1db746171498be76770e5a7b2ab4456017c2ef5)
创建，明确不修改固件或测试实现，不重跑构建、烧录、HIL、T01/T02/T18，
也不处理物理装配和展示材料。

## 项目做什么

- **本地占用状态**：`VACANT` / `OCCUPIED` / `UNKNOWN`，输入来自 HLK-LD2410C-P。
- **用户模式**：`NORMAL` / `QUIET` / `NIGHT`，由按键、Matter `ChangeToMode`
  或配置的夜间时间窗口驱动。
- **环境告警**：`OK` / `ALERT`，由 DHT22 温湿度阈值在本地计算；首版不创建
  虚构的 Matter 环境 Endpoint。
- **连接方式**：BLE 仅用于 commissioning/维护，正常业务链路为 Matter over Wi-Fi。
- **隐私边界**：不使用摄像头，不上传原始雷达数据，不保存可识别个人身份的数据，
  不接触 110/220 V 市电负载。

三个状态维度并行计算，互不覆盖。状态、端点和离线语义分别见
[状态模型](docs/state-model.md)、[Matter 数据模型](docs/matter-data-model.md)
和[配网生命周期](docs/commissioning-lifecycle.md)。

## 已验证的结果摘要

以下数字来自既有 PSRH-042/043 记录，不是 PSRH-044 新执行的测试：

| 结果 | 数值 | 追溯入口 |
|---|---:|---|
| 集成 Host tests | **130/130 PASS** | [PSRH-043 Host 结果](tests/evidence/PSRH-043_host_test_result.md)，绑定 `c2a0ff0` / `0d403f1` 集成证据 |
| ESP32-C6 集成目标构建 | **1498/1498 PASS** | [PSRH-043 build 结果](tests/evidence/PSRH-043_build_result.md) |
| 与 fresh compatible baseline 的 App BIN 增量 | **+1,472 B** | [PSRH-043 resource measurement](tests/evidence/PSRH-043_resource_measurement.md) |
| 固定诊断 `.bss` 增量 | **+1,920 B** | 同上；`s_task_records` 768 B + `s_task_status` 1,152 B |
| 资源快照 | **16/16，capacity 32，`truncated=no`** | [PSRH-043 HIL 证据](tests/evidence/PSRH-043_hardware_deferral.md) |
| Controller 模式闭环 | **`0 → 1 → 0`，重复两轮** | [PSRH-042 ChangeToMode HIL](tests/evidence/PSRH-042_controller_change_to_mode_hil_20260809.md) |
| 集成镜像 App BIN | **1,906,400 B** | SHA-256 见 [PSRH-043 HIL 证据](tests/evidence/PSRH-043_hardware_deferral.md) |

PSRH-043 的最终 HIL 记录还覆盖了授权 BLE re-commissioning、DHT22 正常/故障/
恢复、受控 Wi-Fi 断开/恢复、Matter CASE/属性读取和真实 USB 断电恢复。
这些是“记录的精确镜像和受控环境下通过”，不是对所有硬件、网络、控制器或
生产场景的保证。

## 证据与收口入口

- [项目收口说明](docs/project-closeout.md)：最终范围、提交绑定、量化结果、限制和不宣称边界。
- [中文/英文简历与面试材料](docs/resume-materials.md)：可直接复用但保持证据边界的求职表述。
- [测试与证据索引](tests/README.md)：按任务和测试状态组织的证据地图。
- [项目计划书](docs/project-plan.md)：阶段计划、实际收口和 Phase 6 范围调整。
- [PSRH-042 任务契约](agent/tasks/PSRH-042.yml)：Matter v1.5 适配与 PARTIAL 边界。
- [PSRH-043 任务契约](agent/tasks/PSRH-043.yml)：可靠性、资源和最终 HIL 证据。
- [PSRH-044 任务契约](agent/tasks/PSRH-044.yml)：本次文档-only 交付的边界和验收项。

仓库只提交脱敏 Markdown 摘要。原始串口/控制器日志、构建产物、Fabric
storage 和受控 manifest 位于任务契约记录的仓库外 artifact 目录；凭据、
setup payload、私钥、MAC、完整地址和原始日志不提交。

## 已知限制与不宣称

- T01、T02、T18 在本次收口中**没有补测**；T08 未形成 PASS，T11 和 T20 未执行，
  T13 只有运行窗口观察、没有执行 watchdog fault injection；这些项目不能在简历
  或面试中写成已通过。
- PSRH-042 的 T14、T17、T19 以及设备重启后的 EP1/EP2 同时保留证据仍是
  **PARTIAL**，不能因 PSRH-043 的另一组授权 HIL 记录而合并改写为 PASS。
- 24 小时稳定性、功耗、量产一致性、OTA/A-B 回滚、第三方生态自动化兼容性、
  医疗/生命监测、专业安防和安全认证均不在已完成声明内。
- 洞洞板、外壳、线束、装配照片、架构图和演示视频在 PSRH-044 中明确排除，
  不把开发板加模块的状态写成完成的消费电子产品。
- 本项目展示的是可验证原型工程能力，不宣称产品级可靠性、隐私合规认证或
  面向生命安全的控制能力。

## 目录

```text
PrivacySense-Matter-Room-Hub/
├─ README.md                         # 当前项目入口和边界
├─ AGENTS.md                         # AI 协作与安全规则
├─ agent/
│  ├─ tasks/                         # PSRH-042/043/044 任务契约
│  └─ handoff_templates/             # Builder 交接模板
├─ docs/
│  ├─ project-plan.md                # 分阶段计划与 Phase 6 决策
│  ├─ project-closeout.md            # 最终收口与证据追溯
│  ├─ resume-materials.md             # 中英文简历、面试提纲、量化清单
│  ├─ state-model.md                 # 并行三维度状态模型
│  ├─ matter-data-model.md           # EP0/EP1/EP2 与 Matter 语义
│  ├─ task-architecture.md           # 任务、队列和资源边界
│  ├─ commissioning-lifecycle.md     # BLE/Wi-Fi/Matter 生命周期
│  ├─ multi-agent-development.md     # 任务状态与独立审查流程
│  ├─ interfaces/                    # 已批准公共接口
│  ├─ approved_sources/              # 资料与版本索引
│  └─ adr/                           # 架构决策记录
├─ firmware/                         # ESP-IDF/Matter 固件
├─ hardware/                         # BOM、连接表和硬件设计资料
└─ tests/
   ├─ README.md                      # 测试与证据索引
   ├─ evidence/                      # 脱敏 Markdown 证据
   └─ host/                          # Host 测试实现
```

## 协作与验证

开发遵循“任务契约 → 隔离实现 → 机器验证 → 独立 Reviewer → Human Lead 决定”的
流程。具体边界见 [AGENTS.md](AGENTS.md) 和[多 Agent 协作流程](docs/multi-agent-development.md)。
PSRH-044 的剩余门槛是文档链接、格式、事实一致性和独立 Reviewer 审查；它不
触发新的固件构建或硬件验证。
