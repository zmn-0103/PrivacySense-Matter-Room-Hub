# PrivacySense Matter Room Hub

无摄像头房间状态中枢，面向消费电子与智能家居场景。

## 项目定位

设备使用毫米波雷达和环境传感器，在本地识别房间状态，通过 BLE 完成首次配网，通过 Matter over Wi-Fi 接入智能家居自动化。

状态采用并行模型，三个维度独立计算不互相覆盖：

- **占用状态**：`VACANT`（无人）/ `OCCUPIED`（有人）/ `UNKNOWN`（传感器失效）
- **用户模式**：`NORMAL` / `QUIET`（按键切换）/ `NIGHT`（时段自动切换）
- **环境告警**：`OK` / `ALERT`（DHT22 温湿度超阈值；CO2 为后续可选扩展）

详见 [状态模型](docs/state-model.md)。

## 首版范围

- 主控：已购 ESP32-C6-DevKitC-1-N16；已通过 `esptool.py flash_id` 确认 ESP32-C6 和 16 MB Flash，板级丝印版本仍待装配时记录，详见 [连接表](hardware/connection-table.md)。
- 无线：BLE commissioning，Matter over Wi-Fi。
- 传感器：已购 HLK-LD2410C-P 毫米波雷达和 AM2302/DHT22 温湿度传感器；现有光敏模块仅作后续明暗扩展；CO2 为可选扩展。
- 交互：开发板板载 GPIO8 RGB LED、Boot 按键，以及现有 SSD1306 OLED（首版可启用，不阻塞核心功能）。
- 系统要求：断网本地运行、Wi-Fi 重连、配置持久化、看门狗、OTA 安全边界（见 [OTA 安全边界](docs/ota-safety.md)）。
- 隐私要求：不使用摄像头，不上传原始雷达数据，不保存可识别个人身份的数据。

首版不同时实现 Thread 和 Wi-Fi，不接触市电负载，不把设备设计成全屋 Matter Controller。

## 项目目录

```text
PrivacySense-Matter-Room-Hub/
├─ AGENTS.md
├─ README.md
├─ .gitignore
├─ docs/
│  ├─ hardware-selection.md   # 硬件选型清单
│  ├─ development-environment.md # 从零搭建 WSL2/ESP-IDF/ESP-Matter
│  ├─ project-plan.md          # 项目计划书
│  ├─ state-model.md           # 状态模型（并行三维度）
│  ├─ matter-data-model.md     # Matter 数据模型
│  ├─ task-architecture.md     # 任务架构
│  ├─ commissioning-lifecycle.md # 配网与断网恢复生命周期
│  └─ ota-safety.md            # OTA 安全边界
├─ firmware/       # ESP-IDF/Matter 固件代码（由 Builder AI 创建）
├─ hardware/       # 连接表、BOM、装配记录和外壳资料
│  ├─ connection-table.md      # GPIO/UART/I2C 连接表
│  └─ BOM.csv                  # 冻结 BOM
├─ tests/          # 测试用例、测试记录和缺陷复现资料
│  ├─ README.md                # 测试矩阵
│  └─ evidence/                # 测试证据（脱敏 Markdown）
└─ tools/          # 项目辅助脚本
```

项目已完成主要需求文档、Ubuntu 24.04 WSL2 工具链安装和固件骨架创建，可以进入 Builder AI 功能实现阶段。尚未完成的 Matter commissioning、干净环境下 `idf.py size` 复核、传感器实机测试和上游 commit 记录不阻塞开始写代码，但必须在对应阶段验收前补齐。环境现状见 [开发环境搭建流程](docs/development-environment.md) 和 [环境证据](tests/evidence/dev-env-config-2026-07-17.md)；其他设计见 [硬件清单](docs/hardware-selection.md)、[连接表](hardware/connection-table.md)、[计划书](docs/project-plan.md)、[状态模型](docs/state-model.md)、[Matter 数据模型](docs/matter-data-model.md) 和 [AI 协作约束](AGENTS.md)。

本项目用于学习和求职作品展示，不宣称具备医疗、安防或生命安全能力。自动化动作必须允许用户关闭，并提供手动控制方式。
