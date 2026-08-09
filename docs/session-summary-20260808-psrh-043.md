# PSRH-043 会话总结

## 当前状态

- Builder Lead：Human Lead
- Builder：Codex (Builder AI)
- 独立 Reviewer：待分配，必须与 Builder 独立
- Integration 分支：agent/psrh-043-phase5-integration
- Integration Worktree：PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-integration
- 集成基线：5044f9f854efdd4a9da899a357682c71605ec707
- PSRH-043 source tip：433c5ec912716432b566879e20f37ec1af8ef078
- Firmware merge：c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Fresh integrated build HEAD：0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- 当前状态：INTEGRATION
- 静态集成、Host、目标构建、资源测量和运行时有界快照已通过；协议/传感器/电源 HIL 仍是硬门禁
- 未合并到 main，未 push，未创建 PR

本文件由任务契约的 owned_paths 和 delivery_metadata 明确声明。任务契约、
证据 Markdown、受控外部产物目录及 evidence-files.sha256 构成同一收口
链路。

## 范围和约束

本会话只处理 PSRH-043 阶段 5 的告警清理、资源/复位诊断、Host 测试、
集成构建、资源增量绑定和证据收尾。

保持不变的范围：

- 不修改 firmware/main/matter_app.cpp 或 firmware/main/state_machine.c；
- 不引入 PSRH-042 未提交代码；
- 不修改 watchdog、OTA、分区、复位、安全策略或离线行为；
- 未经授权不烧录；已授权的本轮 HIL 只记录实际结果，不宣称未执行的硬件测试为 PASS；
- 仅只读查看 PSRH-042 分支，不修改其 Worktree。

两次 fresh 构建均使用相同工具链和命令：

~~~sh
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2
idf.py -B <fresh-build-dir> set-target esp32c6
idf.py -B <fresh-build-dir> reconfigure
ninja -C <fresh-build-dir> -j2 all
idf.py -B <fresh-build-dir> size
~~~

## 实现

firmware/main/health_diag.c/.h 提供无动态分配的有界诊断路径：

- 当前空闲堆和历史最低空闲堆；
- normal/software/panic/watchdog/brownout/unknown 复位分类；
- FreeRTOS 任务栈高水位及聚合最小值；
- 固定容量 32 个任务记录；
- tasks、captured、capacity 和 truncated 写入日志；
- 任务名在调度器暂停期间复制到模块自有固定缓冲区；
- 保留现有 network 任务周期诊断节奏；
- 不改变任务栈、队列、分配、watchdog、reset、safe-mode 或 offline 行为。

PSRH-043 同时移除了 baseline 中 network.c 的无调用 command_is_link_event
项目 warning；没有增加 compiler warning suppression。

## Fresh 基线和集成构建

使用相同 fresh 配置重新构建 5044f9f，确认权威基线 App BIN 为
0x1d1120（1,904,928 B）。此前记录的 0x1d0d80 没有被继续使用。

集成构建使用 branch HEAD 0d403f1，Firmware 实现来自 c2a0ff0；HEAD 后续
变更仅为证据元数据。结果：

- baseline：1497/1497，exit 0；
- integrated：1498/1498，exit 0；
- integrated size：App BIN 0x1d16e0，Flash Code 1,753,510 B，
  DIRAM 243,701 B，total image 1,906,295 B；
- integrated App BIN SHA-256：
  3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c；
- integrated App ELF SHA-256：
  c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc；
- integrated App MAP SHA-256：
  1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f。

受控外部产物目录：

~~~text
/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout
~~~

完整产物、脱敏环境/configure/build/size/Host/warning 日志和 SHA-256 清单
均保存在该目录。顶层 evidence-files.sha256 的 SHA-256 为：

~~~text
929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143
~~~

## 资源增量

| Measurement | Baseline 5044f9f | Integrated | Delta |
|---|---:|---:|---:|
| Application BIN | 1,904,928 B (0x1d1120) | 1,906,400 B (0x1d16e0) | +1,472 B |
| Flash Code | 1,752,036 B | 1,753,510 B | +1,474 B |
| DIRAM/.bss | 241,781 / 88,968 B | 243,701 / 90,888 B | +1,920 B |
| .data | 18,077 B | 18,077 B | +0 B |
| LP SRAM | 24 B | 24 B | +0 B |
| Total image | 1,904,821 B | 1,906,295 B | +1,474 B |

health_diag.c.obj 贡献 872 B Flash Code 和 1,920 B DIRAM .bss；固定符号
s_task_records 为 768 B，s_task_status 为 1,152 B。主应用任务栈常量未增加。

## Host 和 warning

Host 命令 make clean && make -j2 all 在 GCC 15.2.0、-Wall -Wextra -Werror
下 exit 0，130/130 通过：

| Suite | 通过数 |
|---|---:|
| occ_sm | 17 |
| ui_rgb | 20 |
| button_mode | 10 |
| env_alert_sm | 21 |
| night_window_sm | 25 |
| network_reconnect_sm | 34 |
| health_diag | 3 |
| **总计** | **130** |

warning 证据来自保留日志而非只看 exit code：

- baseline 有 project-owned network.c unused warning；
- integrated 已无该 project-owned compiler warning；
- firmware CMake 的 VERSION 3.5 boilerplate compatibility warning 已明确分类；
- ESP-Matter/ConnectedHomeIP maybe-uninitialized、重复
  SEC_CERT_DAC_PROVIDER 以及 ESP-IDF/managed-component compatibility warning
  保持可见、未抑制。

## Runtime/HIL 硬门禁

本轮已在授权 Hardware Lab 使用精确 App BIN
`3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c` 烧录。
烧录日志 `integrated-c2a0ff0/hil-flash.log` SHA-256 为
`8b84eef08f8ed1ae133eb8d0751e2e042dbb1fda270dd70c3a331f5b4bb47719`；四个
区域均报告 `Hash of data verified`，应用写入 1,906,400 B，exit 0。

主要串口日志 `hil-runtime-02.log` SHA-256 为
`624ddcd33ad6440b8057bbaaaf17f3de64c946506132f88217368edc2ff19c7a`。它捕获：

| 快照 | tasks/captured | capacity | truncated | free heap | min-free heap | min HWM |
|---|---:|---:|---|---:|---:|---:|
| boot | 4/4 | 32 | no | 272460 | 272460 | 1648 B |
| tasks_ready | 17/17 | 32 | no | 134912 | 119336 | 1156 B |
| network_periodic（约 32 s） | 16/16 | 32 | no | 139660 | 119340 | 1276 B |
| network_periodic（约 62 s） | 16/16 | 32 | no | 139660 | 119340 | 1276 B |
| network_periodic（约 92 s） | 16/16 | 32 | no | 139468 | 119340 | 1276 B |

所有任务身份和 HWM 已在 `tests/evidence/PSRH-043_resource_measurement.md`
及原始脱敏日志中登记。周期诊断期间未见 TWDT failure、panic、Guru
Meditation、abort、stack overflow 或 protocol timeout；`ingress_overruns=0`。
普通复位后的恢复日志 `hil-runtime-recovery-01.log` SHA-256 为
`6969a14b3cf8d7e572ff11f7ecf149a4f8ea900ff3544612301cb960e2d76cce`，同样捕获
完整 boot/tasks_ready/network_periodic 且均为未截断。

协议结果：

- Wi-Fi 启动阶段观察到断开后重新获 IP、状态恢复为 connected，并发布 Matter mDNS；未执行受控的已连接状态 AP 断开，因此该子门禁为 PARTIAL。
- LD2410C radar heartbeat 正常；DHT22 连续 parse fail 并达到 3 次失败阈值，未得到有效样本，也未捕获恢复，故环境传感器 normal/recovery 为 FAIL/NOT_CAPTURED。
- 同一持久化 controller storage 的 Matter EP1 Occupancy 读取为 `1`，EP2 CurrentMode 读取为 `0`；ModeSelect `NORMAL → QUIET → NORMAL` 回读 `1 → 0`；普通复位后两项再次读取成功。
- BLE host sync、commissioning-ready 启动、已 commission 后停止 advertising 以及 BLE deinit/memory reclaim 已观察；未执行需要 factory reset/setup payload 的重新 commissioning。
- 当前 USB/RTS 普通复位不等于已验证的 power-cycle，实验室没有受控电源继电器，因此 power-cycle recovery 为 NOT_CAPTURED。

Runtime resource gate 已 PASS，但整体 HIL 尚未闭环。若后续任何真实采集出现
`truncated=yes`，必须修正容量、重新构建并用新精确 BIN 重测，不能继续验收。
阶段 5 仍为 `INTEGRATION`；独立 Reviewer、Human Lead，以及 DHT22 recovery、
受控 Wi-Fi disconnect/recovery 和 power-cycle 证据仍是最终门禁。
