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
- 静态集成、Host、目标构建和资源测量已通过；运行时/HIL 是硬门禁
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
- 不烧录、不宣称未执行的硬件测试为 PASS；
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
77b3bdb14380e6c508c803179a8329bb1ff40c8d533af4a0ce2060d44770e6be
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

本 integration worktree 没有板卡、串口、调试器或硬件租约，因此以下字段
均为 NOT_CAPTURED，不得解释为 PASS：

- boot、tasks_ready、network_periodic；
- tasks <= 32、captured == tasks、truncated=no；
- free_heap、minimum_free_heap、各任务 HWM；
- 周期诊断期间无 TWDT、panic、protocol timeout；
- Wi-Fi、传感器、Matter/BLE smoke/recovery；
- power-cycle 和 watchdog observation。

Hardware Lab 必须使用上述精确 integrated App BIN hash，并把脱敏串口/HIL
日志与 evidence-files.sha256 绑定。若真实采集 truncated=yes，必须先修正
诊断容量、重新构建同一流程并重测，不能继续验收。

因此当前只能确认静态集成和目标构建通过；阶段 5 尚未 fully PASS。独立
Reviewer、Human Lead 和授权 Hardware Lab 证据仍是后续门禁。
