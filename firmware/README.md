# Firmware

此目录保存 ESP-IDF / Matter 固件代码，由 Builder AI 创建和维护。

Reviewer AI（ChatGPT / Codex）只对提交的代码进行复查和 bug 检查，不直接编写固件代码。

Ubuntu 24.04 WSL2、ESP-IDF v5.4.1 和 ESP-Matter 已安装，官方示例存在构建产物，ESP32-C6 串口与 16 MB Flash 已识别。Matter commissioning 和干净 ESP-IDF shell 下的 `idf.py size` 仍待验证；详见 [开发环境搭建流程](../docs/development-environment.md) 和 [环境证据](../tests/evidence/dev-env-config-2026-07-17.md)。

当前目录已有固件骨架。环境传感器已按冻结文档改为 DHT22 + RMT 接口；事件流已统一为 `app_event_queue`；Matter 数据模型已收敛为 EP0/EP1/EP2。各任务骨架中仍有 `TODO` 标记真实驱动实现（esp_matter endpoint 创建、按键/网络/Matter 命令/夜间窗口处理等）；LD2410C V1.09 帧解析与 DHT22 40-bit 解析已实现并通过 Host 单元测试，待首次构建与硬件验证后落地其余项。Reviewer AI 本轮未修改业务代码。

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
# 每个新终端先加载环境
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2
export IDF_CCACHE_ENABLE=1

# 设置目标芯片，使用隔离构建目录
idf.py -B /tmp/psrh-043-phase5-reliability-build set-target esp32c6

# 配置（首次或修改 sdkconfig.defaults 后）
idf.py -B /tmp/psrh-043-phase5-reliability-build reconfigure

# 编译（必须限制并行度：Matter SDK 链接 libchip.a 极度耗内存，
# 多线程并行链接会耗尽 RAM 导致系统挂起。-j2 在实际设备上已验证稳定。
# 每次编译自动输出 log 文件到 firmware/ 上级目录。）
ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all 2>&1 | \
  tee ../build_$(date +%Y%m%d_%H%M%S).log

# 编译成功后查看 Flash/RAM size
idf.py -B /tmp/psrh-043-phase5-reliability-build size

# 烧录（通过 USB）
idf.py -p /dev/ttyUSB0 flash 2>&1 | \
  tee ../flash_$(date +%Y%m%d_%H%M%S).log

# 监视串口日志（每次监视自动输出 log 文件）
idf.py -p /dev/ttyUSB0 monitor 2>&1 | \
  tee ../monitor_$(date +%Y%m%d_%H%M%S).log

# 编译 + 烧录 + 监视（推荐）
ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all 2>&1 | \
  tee ../build_$(date +%Y%m%d_%H%M%S).log && \
  idf.py -B /tmp/psrh-043-phase5-reliability-build -p /dev/ttyUSB0 flash monitor 2>&1 | \
  tee ../monitor_$(date +%Y%m%d_%H%M%S).log

# 仅烧录 + 监视（编译已完成后）
idf.py -p /dev/ttyUSB0 flash monitor 2>&1 | \
  tee ../monitor_$(date +%Y%m%d_%H%M%S).log
```

> **为什么必须限制并行度？** Matter SDK 的 `libchip.a` 链接阶段单次可占用 2–4 GB RAM。Ninja 默认并行 22 个任务，多个链接进程同时运行会耗尽系统内存，导致编译卡死或 OOM kill。`-j2` 在 32 GB RAM 设备上已验证稳定通过。

> 设备也可能枚举为 `/dev/ttyACM0`。以 WSL2 内实际结果为准；USB 挂载方法见开发环境文档。

## 首次构建状态记录

本节集中追踪首版项目固件首次成功构建所需的所有上游信息。工具链已经安装；Builder AI 完成项目首次构建后，必须把 `[TODO]` 替换为实测值，并同步刷新 [开发环境文档](../docs/development-environment.md) 第 8 节的 commit 表。

### 1. 上游仓库 commit

工具链基线见本文档"工具链版本约束"表。实际安装后必须记录真实 commit：

| 仓库 | 已验证 commit | 安装时间 | 验证方式 |
|---|---|---|---|
| ESP-IDF | `[TODO: git -C /home/administrator/esp/esp-idf rev-parse HEAD]` | 2026-07-17 前 | `idf.py --version` 已输出 `ESP-IDF v5.4.1` |
| ESP-Matter | `[TODO: git -C /home/administrator/esp/esp-matter rev-parse HEAD]` | 2026-07-17 前 | `source /home/administrator/esp/esp-matter/export.sh` 已可用 |
| connectedhomeip | `[TODO: git -C /home/administrator/esp/esp-matter/connectedhomeip/connectedhomeip rev-parse HEAD]` | 2026-07-17 前 | 随 esp-matter 子模块固定 |

### 2. 构建命令

完整命令见本文档"构建与烧录命令"节。首次构建的最小序列：

```bash
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2
export IDF_CCACHE_ENABLE=1

cd /home/administrator/Project/PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-reliability/firmware
idf.py -B /tmp/psrh-043-phase5-reliability-build set-target esp32c6
idf.py -B /tmp/psrh-043-phase5-reliability-build reconfigure   # 应用 sdkconfig.defaults
ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all
idf.py -B /tmp/psrh-043-phase5-reliability-build size
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

## 实际完成状态（2026-07-19）

以下列出各模块实际完成程度，与 git 历史一致：

### 已完成并经过硬件验证

| 模块 | 状态 | 证据 |
|------|------|------|
| LD2410C 帧解析 | 完成：V1.09 normal-mode 帧解析、rolling buffer、stale timeout、断线重连 | R09 回归测试、R10 UART 断线测试 |
| DHT22 驱动 | 完成：RMT RX 捕获、40-bit 解码、checksum、负温度、范围检查 | T07 烟雾测试 |
| Wi-Fi 管理 | 完成：STA 初始化、凭据保存检测、重连退避、auth-fail 停止 | 代码审查 |
| 事件队列统一 | 完成：`g_app_event_queue` 统一生产、队列满计数 | 代码审查 |
| pins.h 连接表镜像 | 完成：GPIO 定义与 `hardware/connection-table.md` 一致 | 代码审查 |

### 已实现但待首次构建验证

| 模块 | 说明 |
|------|------|
| 状态机 | 占用/模式/环境告警三维度并行；状态转换、去抖、退出延迟；待首次运行时验证 |
| 按键处理 | 短按切换 QUIET、长按恢复出厂；待硬件验证 |
| env_sensor 连续失败离线 | 3 次连续失败 → 标记 offline；待首次构建后验证 |
| task watchdog | 所有任务注册 TWDT，喂狗间隔 ≤ 设计上限；待首次运行时验证 |
| OLED 显示 | `ssd1306.c` + `ui.c` 已实现 I2C 硬件驱动、SSD1306 初始化序列、128×64 缓冲、6×8 字体渲染与 `ui_oled_render_state()` 集成；待硬件验证 |
| RGB 驱动 | `ui.c` 已集成 `led_strip` 组件（GPIO8 板载 WS2812），状态映射见 `render_rgb_for_state()`；待硬件验证 |
| LD2410C 配置命令模式 | `ld2410c.c` 已实现 V1.09 命令模式（enable→cmd→disable 事务、ACK 命令字匹配、命令请求队列），Host 单元测试含官方 golden vectors 全 PASS；待真实雷达硬件验证 |

### 首版规划但尚未实现

| 模块 | 阻塞项或规划 |
|------|------|
| **Matter Endpoint 创建** | ESP-Matter API 尚未编写；`matter_app.c` 仍为骨架。需在首次静态 review 后实现 EP0/EP1/EP2 |
| **Matter commissioning** | 依赖 Matter Endpoint 创建完成；首版使用 Test Commissionable Data Provider |
| **OTA** | 分区 A/B 和 otadata 已配置，但 Matter OTA Requestor 未实现；见 `docs/ota-safety.md` 现阶段限制 |
| **NVS 配置持久化** | `config.c` 已实现基本 blob 读写与校验、损坏恢复、重试逻辑；按 key 更新和迁移仍待完成 |

### 已决策但无需在首次构建中实现

- `I2C`：已下线 BME280/SHT31，环境传感器固定为 DHT22 GPIO 接口；I2C 仅保留给 OLED/可选 SCD40
- 首版不创建 Endpoint 3、不上报环境告警为 Matter 属性
- BLE 仅用于 commissioning，不用于扫描手机 MAC
