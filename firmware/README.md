# Firmware

此目录保存 ESP-IDF / Matter 固件代码，由 Builder AI 创建和维护。

Reviewer AI（ChatGPT / Codex）只对提交的代码进行复查和 bug 检查，不直接编写固件代码。

Ubuntu 24.04 WSL2、ESP-IDF v5.4.1 和 ESP-Matter 已安装，官方示例存在构建产物，ESP32-C6 串口与 16 MB Flash 已识别。Matter commissioning 和干净 ESP-IDF shell 下的 `idf.py size` 仍待验证；详见 [开发环境搭建流程](../docs/development-environment.md) 和 [环境证据](../tests/evidence/dev-env-config-2026-07-17.md)。

当前目录已有固件骨架。环境传感器已按冻结文档改为 DHT22 + RMT 接口；事件流已统一为 `app_event_queue`；Matter 数据模型已收敛为 EP0/EP1/EP2。各任务骨架中的 `TODO` 标记真实驱动实现（LD2410C V1.09 帧解析、DHT22 40-bit 解析、esp_matter endpoint 创建等）尚待 Builder AI 在首次构建验证后逐步落地。Reviewer AI 本轮未修改业务代码。

## 工具链版本约束

以下版本为首版冻结约束。Builder AI 首次提交前必须确认环境一致，并提交可审查的 `sdkconfig.defaults`。

| 组件 | 版本 | 来源 | 锁定方式 |
|---|---|---|---|
| ESP-IDF | v5.4.1 | https://github.com/espressif/esp-idf | Git tag `v5.4.1` + 安装后记录 commit |
| esp-matter | Matter 1.5 基线 | https://github.com/espressif/esp-matter | 已安装；目标分支 `release/v1.5`，实际分支与 commit 待记录 |
| connectedhomeip | 随 esp-matter 子模块 | esp-matter 依赖 | Git submodule (pinned by esp-matter) |
| 工具链 | 由 ESP-IDF v5.4.1 安装脚本提供 | ESP-IDF 安装器 | 不复用 Windows/STM32 工具链 |
| CMake/Ninja/Python | 由 ESP-IDF 环境提供或校验 | WSL2 Ubuntu | 不使用 Windows 路径 |
| 目标芯片 | ESP32-C6 | — | `idf.py set-target esp32c6` |
| 开发板 | ESP32-C6-DevKitC-1-N16 | 已购实物 | 已通过 `flash_id` 确认 ESP32-C6 和 16 MB Flash；见 [连接表](../hardware/connection-table.md) |

> 如 ESP-IDF 或 esp-matter 有更新的稳定版本，可在评审后更新本表。但不使用 `master`/`main` 分支作为实现依据。

## 组件来源与依赖管理

| 组件 | 来源 | 版本锁定 | 说明 |
|---|---|---|---|
| esp-idf 组件 | ESP-IDF 内置 | IDF 版本锁定 | freertos, driver, nvs_flash 等 |
| esp-matter | esp-matter 仓库 | Git tag | Matter 协议栈 |
| LD2410C 驱动 | 项目自研 | — | UART 通信和解析，放在 `components/ld2410c/` |
| AM2302/DHT22 驱动 | 项目自研或经评审的固定版本组件 | 如引入外部组件必须锁定版本 | GPIO2 单总线；优先使用 ESP32 RMT 捕获脉宽，禁止直接移植 STM32 忙等代码 |
| SSD1306 OLED 驱动 | 项目自研或经评审的固定版本组件 | 如引入外部组件必须锁定版本 | I2C0，7-bit 地址预期 `0x3C`；使用硬件 I2C，不移植 STM32 软件 I2C |
| WS2812 驱动 | ESP-IDF `led_strip` 组件 | IDF 版本锁定 | RMT 外设驱动 |

## 默认配置

- 提交 `sdkconfig.defaults` 到版本控制，包含：
  - Flash 大小 16 MB（已由实机 `flash_id` 确认）
  - 分区表（OTA A/B 方案，见 [OTA 安全边界](../docs/ota-safety.md)）
  - Wi-Fi 模式 Station
  - BT 模式 BLE Only（commissioning 后释放）
  - FreeRTOS tick rate 100 Hz
  - Task Watchdog 启用，超时 10 s
- 不提交 `sdkconfig`（由 `sdkconfig.defaults` + `idf.py menuconfig` 生成，已在 `.gitignore` 中忽略）。

## 目录结构（规划）

```text
firmware/
├─ CMakeLists.txt
├─ sdkconfig.defaults
├─ partitions.csv
├─ main/
│  ├─ CMakeLists.txt
│  ├─ main.c              # app_main 入口
│  ├─ state_machine.c     # 状态机实现
│  ├─ state_machine.h
│  ├─ room_state.c         # 共享状态管理
│  ├─ room_state.h
│  ├─ button.c             # 按键处理
│  ├─ button.h
│  ├─ ui.c                 # RGB/OLED 显示
│  ├─ ui.h
│  ├─ network.c            # Wi-Fi 管理
│  ├─ network.h
│  ├─ matter_app.c         # Matter 属性映射
│  ├─ matter_app.h
│  ├─ config.c             # NVS 配置管理
│  ├─ config.h
│  └─ pins.h               # GPIO 定义（来自连接表）
├─ components/
│  ├─ ld2410c/             # LD2410C 驱动
│  └─ env_sensor/          # AM2302/DHT22 驱动与 sensor_env_task
└─ README.md
```

> `pins.h` 中的 GPIO 定义必须与 [连接表](../hardware/connection-table.md) 完全一致，不得硬编码到其他文件。

## 构建与烧录命令

```bash
# 每个新 WSL2 终端先加载环境
source /root/esp/esp-idf/export.sh
source /root/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1

# 设置目标芯片，构建目录放在 WSL2 Linux 文件系统
idf.py -B /root/build/privacy-sense set-target esp32c6

# 配置（首次或修改 sdkconfig.defaults 后）
idf.py -B /root/build/privacy-sense reconfigure

# 编译
idf.py -B /root/build/privacy-sense build

# 烧录（通过 USB）
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 flash

# 监视串口日志
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 monitor

# 编译 + 烧录 + 监视
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 flash monitor
```

> 设备也可能枚举为 `/dev/ttyACM0`。以 WSL2 内实际结果为准；USB 挂载方法见开发环境文档。

## 首次构建状态记录

本节集中追踪首版项目固件首次成功构建所需的所有上游信息。工具链已经安装；Builder AI 完成项目首次构建后，必须把 `[TODO]` 替换为实测值，并同步刷新 [开发环境文档](../docs/development-environment.md) 第 8 节的 commit 表。

### 1. 上游仓库 commit

工具链基线见本文档"工具链版本约束"表。实际安装后必须记录真实 commit：

| 仓库 | 已验证 commit | 安装时间 | 验证方式 |
|---|---|---|---|
| ESP-IDF | `[TODO: git -C /root/esp/esp-idf rev-parse HEAD]` | 2026-07-17 前 | `idf.py --version` 已输出 `ESP-IDF v5.4.1` |
| ESP-Matter | `[TODO: git -C /root/esp/esp-matter rev-parse HEAD]` | 2026-07-17 前 | `source /root/esp/esp-matter/export.sh` 已可用 |
| connectedhomeip | `[TODO: git -C /root/esp/esp-matter/connectedhomeip/connectedhomeip rev-parse HEAD]` | 2026-07-17 前 | 随 esp-matter 子模块固定 |

### 2. 构建命令

完整命令见本文档"构建与烧录命令"节。首次构建的最小序列：

```bash
source /root/esp/esp-idf/export.sh
source /root/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1

cd /home/projects/PrivacySense-Matter-Room-Hub/firmware
idf.py -B /root/build/privacy-sense set-target esp32c6
idf.py -B /root/build/privacy-sense reconfigure   # 应用 sdkconfig.defaults
idf.py -B /root/build/privacy-sense build
```

### 3. 内存摘要

设计预算见 [任务架构](../docs/task-architecture.md) 第 8 节。下表"实际值"列在首次构建后由 Builder AI 填入：

| 项目 | 设计预算 | 实际值 | 来源 |
|---|---|---|---|
| 应用镜像 Flash | 预计 2–3 MB | `[TODO: idf.py build 输出的 app binary size]` | 构建摘要 |
| 总镜像占用（含 bootloader + 分区表） | — | `[TODO]` | 构建摘要 |
| SRAM 使用（静态 + 堆） | ~290 KB | `[TODO: 构建摘要 Used DRAM / Used IRAM]` | 构建摘要 |
| `state_machine_task` 栈高水位 | 6144 B | `[TODO: 串口日志 uxTaskGetStackHighWaterMark]` | 运行时日志 |
| `matter_adapter_task` 栈高水位 | 12288 B | `[TODO]` | 运行时日志 |
| `network_task` 栈高水位 | 8192 B | `[TODO]` | 运行时日志 |
| 其他任务栈高水位 | 见任务表 | `[TODO]` | 运行时日志 |

### 4. 上游 warning

首版不启用 `-Werror`（见 `sdkconfig.defaults` 中 `CONFIG_COMPILER_WARN_WRITE_STRINGS` 注释）。首次构建后必须把以下信息回填：

| 项目 | 内容 |
|---|---|
| ESP-IDF 自身 warning | `[TODO: 首次构建后粘贴 idf.py build 中来自 esp-idf 组件的 warning 摘要]` |
| esp-matter / connectedhomeip warning | `[TODO: 同上，esp-matter 与 connectedhomeip 子模块的 warning]` |
| 项目自身 warning | `[TODO: 来自 firmware/main 与 firmware/components 的 warning，必须修复后再提交]` |
| 已知阻断 issue | `[TODO: 如有上游 GitHub issue 影响，列出链接与规避方案]` |

### 5. 隐私信息处理说明

`sdkconfig.defaults` 和 `partitions.csv` 经审查不包含以下任何信息：

- Wi-Fi SSID / passphrase（运行时通过 BLE commissioning 注入，由 ESP-IDF Wi-Fi NVS 存储）
- Matter setup passcode / discriminator / SPAKE2+ verifier / QR payload（来自 Commissionable Data Provider：开发期用 Test Provider 固定值，量产用 Factory Provider 预置在 factory NVS/Flash；QR/manual-code setup payload 为带外展示给用户扫码/输入，BLE 仅承载后续 PASE commissioning 会话；绝不出现在源码或 sdkconfig 中）
- 设备 MAC 地址（使用芯片出厂 MAC，不在源码中硬编码）
- 任何私钥 / 证书 / `.pem` / `.key`（已在 `.gitignore` 中过滤）

首次构建烧录后产生的 setup payload 仅允许出现在：
1. 设备串口日志（仅 DEBUG 级别，由 `CONFIG_LOG_DEFAULT_LEVEL` 控制）。
2. BLE commissioning 通道（运行时）。

不得将 setup payload、配网码或 MAC 复制到 `tests/evidence/` Markdown、提交信息、PR 描述或任何仓库内文件中。`tests/evidence/.gitkeep` 之外的二进制文件已通过 `.gitignore` 过滤。

## Builder AI 第一批代码修改清单（已完成）

以下阻塞项已在本轮按冻结文档实施完毕，每条对应的具体改动见 git 历史：

- `main/pins.h`：增加 DHT22 `GPIO2`，删除 BME280/SHT31 首版地址定义；保留 OLED `0x3C` 和可选 SCD40 `0x62`。
- `components/env_sensor/`：I2C 接口已下线，改为 DHT22 GPIO/RMT callback 接口；40-bit 解析、checksum、负温度、合理范围和连续失败计数仍为 `TODO`，待 Builder AI 在首次构建后补齐。
- `main/main.c`：按新 `env_sensor_start(data_gpio, rmt_clk_hz, callback)` 启动 DHT22；I2C 只由 OLED/后续 SCD40 初始化。
- 事件与队列：所有生产者统一发 `app_event_t` 到 `g_app_event_queue`；增加 `g_matter_report_queue`；按 `task-architecture.md §5.3` 修正队列满处理（按键/网络/Matter 不静默丢弃；雷达/环境允许丢弃但需计数 TODO）。
- `main/matter_app.*`：首版只创建 Endpoint 0/1/2；删除任何 Endpoint 3/`stateValue` 计划；EP2 设备类型改为 `0x0027`；`ChangeToMode` 通过 `matter_app_respond_change_to_mode()` 等待状态机结果后返回（≤ 100 ms，超时不得返回 SUCCESS）。
- `sdkconfig.defaults`：`CONFIG_ESP_TASK_WDT_TIMEOUT_S=10`，并核对所有已注册任务的最大喂狗间隔（雷达 ≤500 ms，其余 ≤2 s 或 ≤5 s）。
- OLED：使用 ESP-IDF hardware I2C 和 7-bit `0x3C`；不得移植 STM32 bit-bang I2C 实现。

第一批代码完成后先做静态 review，再在 WSL 中执行首次项目构建；不要在未实现驱动时用空骨架构建结果宣称硬件功能完成。
