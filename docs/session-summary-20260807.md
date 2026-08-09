# PSRH-042 当前对话总结

日期：2026-08-07
协作角色：Builder，模型 `gpt-5.6-luna max`
项目：PrivacySense-Matter-Room-Hub

## 1. 会话目标与协作约束

本次会话围绕 ESP-Matter 1.5 的 Matter 适配、机器验证、开发板烧录和协议语义审查展开。

主要约束：

- 严格按照任务契约和冻结的数据模型工作。
- Builder 在独立分支和 worktree 中实施，Codex 后续审查。
- 基线为 `02e67aa5216529ca83bff32bbf46ac1a8972e48d`。
- ESP32-C6 构建并行任务限制为 `-j2` 或 `-j4`；本次统一使用 `-j2`。
- 开发板已保留 Wi-Fi 和 Matter 配置，不重新配网、不重新配对、不擦除 NVS。
- Radar FeatureMap 是唯一经 Human Lead 明确批准的 Matter 数据模型增量。
- 原始串口日志和敏感凭据不得提交到仓库。

## 2. 任务契约与工作区

任务契约：[`agent/tasks/PSRH-042.yml`](../agent/tasks/PSRH-042.yml)

| 项目 | 当前值 |
|---|---|
| Task ID | `PSRH-042` |
| 状态 | `VERIFYING` |
| Owner | `gpt-5.6-luna max` |
| Reviewer | Codex |
| 分支 | `agent/psrh-042-matter-v15` |
| Worktree | `/home/administrator/Project/PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15` |
| 目标 | 在锁定的 ESP-Matter `release/v1.5` 上完成 API 兼容，保持 EP0/EP1/EP2 数据模型不变，并产生构建、size、测试证据 |
| 外部构建目录 | `/tmp/psrh-042-matter-v15-verification.KBATZe/` |

Human Lead 已批准：

- 从基线 `02e67aa` 在独立 Builder worktree 中继续工作。
- 保留 Radar Occupancy FeatureMap 作为唯一 Matter 数据模型增量。
- 新增 `ChangeToMode` NIGHT 窗口语义防护，修改范围为 `matter_app.cpp` 和 `state_machine.c`，不改变 Matter 数据模型。

## 3. 锁定工具链

| 组件 | 路径 | 版本/提交 |
|---|---|---|
| ESP-IDF | `/home/administrator/esp/esp-idf` | `v5.4.1`, `4c2820d377d1375e787bcef612f0c32c1427d183` |
| ESP-Matter | `/home/administrator/esp/esp-matter` | `a51f624f0735aefd0a9cfe1e0039d68de8ce24e2` |
| connectedhomeip | `/home/administrator/esp/esp-matter/connectedhomeip/connectedhomeip` | `cf84d0360c48dbc194c48b47b09169f302a9745b` |
| Python | ESP-IDF 环境 | `3.14.4` |
| CMake / Ninja | 系统工具 | `4.2.3` / `1.13.2` |

## 4. 已完成的代码工作

### ESP-Matter 1.5 API 兼容

`firmware/main/matter_app.cpp` 已完成以下适配：

- 使用 ESP-Matter 1.5 的 `ModeSelect::SupportedModesManager` API。
- EP2 的静态 SupportedModes 保持为：`Normal=0`、`Quiet=1`、`Night=2`。
- Semantic Tags 使用空列表；模式数据保持静态，避免无界运行时分配。
- 属性回调改用锁定 SDK 的 `attribute::PRE_UPDATE` 类型。
- 修复 C/C++ 聚合结构体初始化，避免未初始化字段。
- 增加经批准的 Radar Occupancy FeatureMap。
- 修复 Occupancy 属性值类型：`esp_matter_uint8()` 改为 `esp_matter_bitmap8()`，解决实机出现的 `err:258` 属性更新失败。

### ChangeToMode NIGHT 语义修正

原风险是：`ChangeToMode` 只异步投递给状态机，状态机此前可以直接把模式切换为 NIGHT；因此控制器可能先收到成功，而本地夜间窗口约束尚未执行。

当前实现：

1. `matter_app.cpp:102-137` 增加同步窗口判断：
   - SNTP 未同步时 fail-closed；
   - 配置读取失败时 fail-closed；
   - 使用当前本地时间和配置的夜间窗口；
   - 复用纯逻辑 `night_window_sm_eval()` 判断是否在窗口内。
2. `room_supported_modes_manager::getModeOptionByMode()` 在 `CurrentMode::Set()` 之前检查 NIGHT；窗口外返回 `InteractionModel::Status::Failure`。
3. `state_machine.c:660-696` 在事件队列交付后再次检查相同策略，禁止非法 NIGHT 状态转换。
4. 已复核锁定 SDK 的 ModeSelect 调用顺序：SDK 先调用 `SupportedModesManager`，失败时不会写 `CurrentMode`。

本次语义修正没有增加 Endpoint、Cluster、Attribute、Mode 值，也没有改变已批准的 Radar FeatureMap。

当前源文件差异哈希（仅 `matter_app.cpp` 和 `state_machine.c`）：

```text
4cca9ae6c3ad89f0794e0b102d673cd8942d39a066f671106a9232c383f9d83f
```

## 5. 自动化测试与构建结果

### Host/组件测试

执行时使用 `-j2`：

```text
make -C tests/host BUILD=/tmp/psrh-042-matter-v15-verification.KBATZe/tests-host-policy-fix -j2 all
make -C firmware/components/ld2410c/test clean
make -C firmware/components/ld2410c/test -j2 test_all
make -C firmware/components/env_sensor/test clean
make -C firmware/components/env_sensor/test -j2 test
```

结果：

- 项目 Host：`127/127 PASS`；
- LD2410C：`40/40 PASS`（解析器 24，事务 16）；
- DHT22：`11/11 PASS`；
- 总计：**178/178 PASS**。

日志哈希：

```text
host-policy-fix.log    63115ed709720224a8ae2c96a9107b3d36bdf00566e9e110a4c176914cbb4919
ld2410c-policy-fix.log f4b251fe84be3e0c51b75afcb7eff50155a58ee7532640c33f3347314a3b4a87
env-policy-fix.log     0cc426439b886a100de7c82e16090153edf3370a754dab9b85be7c0e3fe18704
```

### ESP32-C6 隔离构建

构建目录：

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix
```

环境变量包含：

```text
IDF_CCACHE_ENABLE=1
CCACHE_DIR=/tmp/psrh-042-matter-v15-verification.KBATZe/ccache
NINJAFLAGS=-j2
```

结果：**1497/1497 PASS**。

镜像和 size 摘要：

| 项目 | 结果 |
|---|---:|
| Flash Code | 1,749,534 B |
| 应用二进制 | 1,902,416 B (`0x1d0750`)，应用分区剩余 64% |
| `idf.py size` 总 image | 1,902,319 B |
| DIRAM | 241,581 B，53.43% |
| LP SRAM | 24 B，0.15% |
| Bootloader | 22,128 B，32% free |

日志及镜像哈希：

```text
firmware-policy-fix-build.log  28eb22c2170e21f400d5c8c9ef194051e05221897141ef4750ed85030ffd488b
firmware-policy-fix-size.log   8f0b429d5b132465c9c1c214baeb81bbba2300983014a2961d644da0991d9ebf
privacy-sense-matter-room-hub.bin  58b19591999b98192770742a4de53b5b66ba15ae623213fbf2eaac3fff331aef
privacy-sense-matter-room-hub.elf  38534f53805fbdbb5c98a1b454e6b3348cd279fb09129cccf9e6ee7448584c89
```

## 6. Warning 状态

构建成功，但 warning gate 尚未清洁：

### 项目自身告警

- `firmware/CMakeLists.txt:19`：CMake `cmake_minimum_required` deprecation。
- `firmware/main/network.c:199`：`command_is_link_event` 未使用（`-Wunused-function`）。

两者均不在本任务当前 owned paths 中，且没有被本次修正引入。

### 上游告警

- connectedhomeip Camera 可选字段可能未初始化；
- ColorControl `direction` 可能未初始化；
- `SEC_CERT_DAC_PROVIDER` Kconfig 重复/choice 定义；
- ESP-Matter、ESP-IDF mbedTLS 和 managed component 的 CMake deprecation。

## 7. 开发板烧录与实机启动

用户通过 Windows USB/IP 挂载 CP2102，WSL 中确认 `/dev/ttyUSB0` 可用。策略修正版使用：

```text
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix -p /dev/ttyUSB0 flash
```

结果：**PASS，exit 0**。Bootloader、应用、partition table、OTA data 四段均报告校验成功；未执行 NVS erase、factory reset 或重新配网。

Flash 日志：

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/flash-policy-fix-20260807.log
4422a51d1c7b036c855e503b0cb7e9410f45ae7265caa83bd719eced8f838a0a
```

串口启动日志确认：

- 保存的 Wi-Fi 凭据恢复；
- Matter Fabric 已存在；
- EP0、EP1、EP2 启动；
- STA 获取 IP，IPv4 connectivity established；
- operational `_matter._tcp` mDNS 发布；
- SNTP 时间同步；
- state machine、radar、UI、环境传感器持续心跳；
- 没有 panic、abort、watchdog reset、Occupancy `err:258` 或属性更新失败。

启动监视日志：

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/monitor-policy-fix-boot-20260807.log
cdc398b0f52ddfcb2abd2db9e3ccaa2b520176e4f6e06c356702b6d99ca8c698
```

运行过程中仍看到既有的 `ps_cfg` 配置缺失/尺寸不匹配提示，设备随后使用默认业务配置；这与 Wi-Fi/Matter 凭据恢复是分开的事项。

## 8. 控制器命令 HIL 状态

本机存在已构建的 `chip-tool`，但标准持久化存储为空，没有可访问板上既有 Fabric 的控制器凭据。

已尝试隔离存储目录执行只读命令：

```text
chip-tool modeselect read current-mode 0x1234 2 \
  --storage-directory /tmp/psrh-042-matter-v15-verification.KBATZe/chip-tool-policy-fix-storage
```

结果：`chip-tool` 生成了新的本地测试 Fabric，但在 WSL NAT 网络下 operational mDNS 解析超时，未建立 CASE 会话。该尝试没有写设备属性、配网、恢复出厂或擦除设备。

日志哈希：

```text
1828eca3efc15eb576b3d51e0b64f55920e3e6e5156e395ceaa57f2fd3260228
```

因此当前没有声称“控制器收到窗口外 NIGHT 失败”为实机 PASS。代码、Host、ESP32-C6 构建、刷写和设备启动证据已通过；真实控制器命令 HIL 仍待补充。

## 9. 当前状态与下一步

当前任务必须保持 `VERIFYING`，原因：

1. 真实控制器 `ChangeToMode(NIGHT)` 响应尚未在可用 Matter Fabric/session 上验证；
2. 项目自身 warning gate 尚未清理或获得明确接受；
3. Codex reviewer 尚未完成最终 sign-off。

补齐 HIL 时，应在当前时间处于默认夜间窗口外的条件下，从已有 Matter controller session 执行：

```text
modeselect change-to-mode 2 <node-id> 2
```

预期结果：控制器收到失败响应，EP2 `CurrentMode` 不进入 NIGHT，设备串口出现 NIGHT 窗口拒绝日志。不得通过擦除 NVS 或恢复出厂来绕过当前凭据缺口，除非 Human Lead 另行批准。

机器验证总证据：[`tests/evidence/PSRH-042_machine_verification_20260807.md`](../tests/evidence/PSRH-042_machine_verification_20260807.md)

## 10. WSL mirrored 网络与 Meta Fake-IP 代理修正（2026-08-08）

用户确认：启用 WSL `mirrored` 模式后，Meta 的 TUN/Fake-IP 网卡也被镜像到 WSL，并抢占默认路由。

- Windows 将 `api.openai.com` 解析成 Meta Fake-IP：IPv4 `198.18.0.65`、IPv6 `fdfe:dcba:9876::4a`；
- WSL 优先走 Meta 虚拟网关 `198.18.0.2`，Fake-IP 流量未能从 WSL 正确转发，导致 Codex CLI 直连 HTTPS 超时；
- 不能简单切回 NAT：本项目 Matter 控制器/`chip-tool` 需要 mirrored 模式提供 IPv6、多播和 mDNS 局域网发现能力。

解决方案：保持 WSL `mirrored` 模式，将 WSL 的 HTTP/HTTPS 流量显式送到 Windows 本机 Meta 代理端口 `7897`。用户确认已写入 WSL `~/.bashrc`：

```bash
export HTTP_PROXY=http://127.0.0.1:7897
export HTTPS_PROXY=http://127.0.0.1:7897
export ALL_PROXY=http://127.0.0.1:7897
export NO_PROXY=localhost,127.0.0.1,::1,.local
```

预期效果：

- Codex CLI、Git、依赖下载等 HTTPS 请求通过 Meta 正常出网；
- `NO_PROXY=.local` 使本地 Matter 设备发现不经过代理；
- WSL 继续使用 mirrored，保留 Matter 所需的 mDNS/IPv6 能力；
- `usbipd attach --wsl` 与该网络问题无直接关系，仍可正常挂载 ESP32-C6。
