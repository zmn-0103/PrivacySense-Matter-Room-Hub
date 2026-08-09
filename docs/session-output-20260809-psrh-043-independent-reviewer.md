# PSRH-043 独立 Reviewer 会话总结

## Reviewer 身份与审查边界

- 角色：Independent Reviewer AI
- Reviewer：`gpt-5.6-sol`
- 审查日期：2026-08-09
- 审查分支：`agent/psrh-043-phase5-integration`
- 初次审查 HEAD：`a21cf00bfd8c01162cb3e47977e0e5d67ff82a70`
- 最终复审 HEAD：`4af6c6350e070a3129b63596587d46042cf34148`
- 固件集成提交：`c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- PSRH-042 集成基线：`5044f9f854efdd4a9da899a357682c71605ec707`
- PSRH-043 Builder source tip：`433c5ec912716432b566879e20f37ec1af8ef078`

Reviewer 本轮只执行静态审查、Git 范围核验、已有构建产物和外部证据的
只读校验。Reviewer 未实现或修改固件，未重新编译、烧录或操作硬件；仅按
Human Lead 请求创建了空的独立 Integration 分支/worktree，后续集成、构建和
Hardware Lab 操作由 Builder/Human Lead 完成。

## PSRH-042 前置结论

PSRH-042 已完成独立审查和 Human Lead 收口：

- Reviewer 结论：PASS
- 状态：`READY_TO_MERGE`
- 固定集成输入：`5044f9f854efdd4a9da899a357682c71605ec707`
- 固件实现冻结点：`2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`
- 权威生产镜像 HIL：`0dda51d4aeef2a1717445dcb96c462094b8ed1ec`

PSRH-043 集成以 `5044f9f` 为基线，没有先修改或合并到 `main`。

## PSRH-043 集成审查

集成分支由 `5044f9f` 创建，随后通过 merge commit `c2a0ff0` 引入完整
PSRH-043 历史。`c2a0ff0` 的两个父提交分别为 `5044f9f` 和 `433c5ec`。
治理提交 `433c5ec` 与 PSRH-042 已包含的 `c341e51` 内容等价；完整历史通过
merge endpoint 保留。

Reviewer 确认：

- PSRH-042 的 `matter_app.*`、`state_machine.*` 和冻结接口路径相对
  `5044f9f` 无变化；
- PSRH-042 的 HIL/release CMake 隔离门禁与 PSRH-043 的 `health_diag.c`
  注册同时保留；
- `c2a0ff0` 之后的 `0d403f1`、`e8eaa0d`、`a21cf00`、`4af6c63` 仅修改任务
  合同、总结和证据文档，没有修改固件、Host 测试或工具代码；
- 当前已提交差异通过 `git diff --check`；
- 未发现新的确定性固件逻辑缺陷、越界修改或未解决 merge conflict。

## 静态实现审查

`health_diag` 实现使用固定 32 项静态快照，不在周期路径新增无界动态分配。
任务名在调度暂停期间复制到模块自有缓冲区；任务数、捕获数、容量和
`truncated` 状态均显式记录。当前/最低空闲 heap、reset reason、逐任务栈
高水位和聚合最小值均有日志字段。

Reviewer 对锁定的 ESP-IDF 5.4.1 源码进行了单位和 API 语义核对；当前目标的
`StackType_t` 为字节类型，记录的 HWM 数值与现有 FreeRTOS API 日志一致。
诊断阈值仅输出日志，没有修改 watchdog、reset、safe-mode、OTA、分区、Matter
数据模型或离线行为。

静态资源存储由 ELF 符号验证：

| 符号 | 大小 |
|---|---:|
| `s_task_records` | 768 B |
| `s_task_status` | 1,152 B |
| 合计 `.bss` | 1,920 B |

## 构建、测试和资源证据复核

Builder 使用相同 fresh 构建流程分别构建 `5044f9f` 基线和集成镜像。Reviewer
对保留产物、size 日志、warning 分类日志和外部 manifest 进行了只读复核。

| 项目 | Fresh baseline | Integrated | 增量/结果 |
|---|---:|---:|---:|
| Application BIN | 1,904,928 B | 1,906,400 B | +1,472 B |
| Flash Code | 1,752,036 B | 1,753,510 B | +1,474 B |
| DIRAM | 241,781 B | 243,701 B | +1,920 B |
| `.bss` | 88,968 B | 90,888 B | +1,920 B |
| `.data` | 18,077 B | 18,077 B | +0 B |
| LP SRAM | 24 B | 24 B | +0 B |

- ESP32-C6 target build：1498/1498 PASS
- Host tests：130/130 PASS
- Production configuration：`PSRH_HIL_BUILD=OFF`、`PSRH_RELEASE_BUILD=ON`、
  HIL timing override 为空
- 基线 `network.c` unused-function warning 在集成后消失
- 项目 CMake 兼容性和上游 ESP-Matter/ConnectedHomeIP/Kconfig warnings 已保留、
  分类，未通过新增 suppression 隐藏

集成产物身份：

| 产物 | SHA-256 |
|---|---|
| App BIN | `3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c` |
| App ELF | `c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc` |
| App MAP | `1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f` |

外部证据根目录：

`/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout`

最新 `evidence-files.sha256` 包含 42 项，manifest SHA-256 为
`929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143`。
Reviewer 执行 `sha256sum -c evidence-files.sha256`，42 项全部通过。

后续授权 BLE 证据根目录：

`/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/ble-direct-20260809`

该目录 manifest 包含 8 项，SHA-256 为
`4ef5aa8bd65f3f4dc0ff8f9cc5ccb6e3ccb4957a64c8e0198d631fc4e0b5b6fb`；
Reviewer 复验 8 项全部通过。

最终 HIL closeout 证据根目录：

`/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/hil-closeout-20260809`

该目录 manifest 包含 15 项，SHA-256 为
`1792415f3734a81147d349d384999f0cf454c07ef6a75ea48d4f71e82e5362fb`；
Reviewer 复验 15 项全部通过。三套 manifest 合计 65 项均通过完整性校验。

## Hardware Lab 证据复核

Hardware Lab 烧录了上述精确 App BIN。flash transcript 显示 bootloader、
partition table、OTA metadata 和 application 四个区域均通过数据哈希校验，
application 写入大小为 1,906,400 B，flasher exit code 为 0。

运行时资源快照：

| 快照 | tasks/captured | capacity | truncated | free heap | minimum free heap | minimum HWM |
|---|---:|---:|---|---:|---:|---:|
| boot | 4/4 | 32 | no | 272,460 | 272,460 | 1,648 B |
| tasks_ready | 17/17 | 32 | no | 134,912 | 119,336 | 1,156 B |
| periodic 约 32 s | 16/16 | 32 | no | 139,660 | 119,340 | 1,276 B |
| periodic 约 62 s | 16/16 | 32 | no | 139,660 | 119,340 | 1,276 B |
| periodic 约 92 s | 16/16 | 32 | no | 139,468 | 119,340 | 1,276 B |
| 普通复位后 periodic | 16/16 | 32 | no | 139,348 | 119,256 | 1,268 B |

所有已观察快照均满足：

- `tasks <= 32`；
- `captured == tasks`；
- `truncated=no`；
- 每个捕获任务均有身份和 HWM；
- heap 和 minimum heap 已记录。

周期诊断窗口没有观察到 ingress overrun、TWDT failure、panic、Guru
Meditation、abort、stack overflow 或固件协议 timeout。日志中的
`Stopping the watchdog timer` 是框架正常信息，不是 watchdog failure。

Matter 子项：

- EP1 Occupancy 读取 `1`；
- EP2 CurrentMode 初始读取 `0`；
- ModeSelect `NORMAL -> QUIET -> NORMAL` 后回读 `1 -> 0`；
- 普通复位后，同一 controller storage 重新建立 CASE 并读取 Occupancy `1`、
  CurrentMode `0`；
- 一次并发 controller probe 返回 `Resource is busy`，顺序重试成功。该记录被
  保留，不解释为固件周期诊断超时。

## 后续 Hardware Lab 门禁复核

Builder 使用同一精确 App BIN 补齐了初审时尚未关闭的门禁。Reviewer 对新增
manifest 和原始日志执行只读复核：

- BLE：仅擦除 `nvs` 与 `ps_cfg`，未重刷或修改固件；日志记录 BLE GATT、PASE、
  NOC/CASE、Wi-Fi 配置和 `CommissioningComplete errorCode=0`。新 Fabric 下
  Occupancy=`1`，CurrentMode 完成 `0 -> 1 -> 0`，普通 RTS reset 后恢复通过。
- DHT22：接线修复后持续记录有效温湿度样本；移除 DATA 后记录三次连续失败、
  `online -> offline` 和 `P1-sensor-fail`；恢复 DATA 后记录有效样本、
  `offline -> online` 和后续稳定样本。
- Wi-Fi：稳定联网后的受控 AP outage 期间，DHT22、radar、状态机、UI 和周期资源
  快照持续运行；AP 恢复后重新获 IP、发布 operational `_matter._tcp`、建立 CASE，
  并回读 Occupancy=`1`、CurrentMode=`0`。
- Power-cycle：`/dev/ttyUSB0` 从 17:41:51 至 17:42:20 消失 29 秒，构成真实板卡
  断电而非 RTS reset；重新枚举后 Fabric、mDNS、CASE、属性、DHT22、radar 和
  资源快照均恢复。

新增快照均为 16/16、capacity 32、`truncated=no`、`ingress_overruns=0`；AP outage
期间最低 HWM 为 1,180 B，power-cycle 后最低 HWM 为 1,280 B。新增串口证据未
命中 panic、Guru Meditation、abort、stack overflow、TWDT failure、固件协议
timeout 或 `truncated=yes`。

Reviewer 另确认精确 App BIN SHA-256 仍为
`3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c`。
`a21cf00..4af6c63` 及最终复审时的工作区差异仅涉及任务合同、证据和总结文档，
未改变固件、Host 测试、工具代码或镜像身份。

## Reviewer 最终结论

- 静态代码与集成范围：PASS
- Fresh 构建、Host 测试、warning 分类和静态资源测量：PASS
- 精确镜像烧录与运行时资源快照：PASS
- BLE 重新 commissioning：PASS
- DHT22 正常、故障与恢复：PASS
- 受控 Wi-Fi 断开与恢复：PASS
- 真实 power-cycle 与 Matter/controller 恢复：PASS
- 完整 Hardware Lab：PASS
- 最终独立 Reviewer 结论：**PASS**
- Human Lead 接受：**ACCEPTED**（2026-08-09，用户明确确认）
- PSRH-043 当前治理状态：`READY_TO_MERGE`

本结论绑定最终复审 HEAD `4af6c6350e070a3129b63596587d46042cf34148`、精确
App BIN 和上述三套 manifest。Human Lead 已明确接受，可进行收口提交和后续
合并。若后续出现任何固件、镜像、manifest 或实质证据变化，必须重新绑定哈希
并对受影响范围执行新一轮独立审查。
