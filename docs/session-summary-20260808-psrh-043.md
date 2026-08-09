# PSRH-043 会话总结

## 会话状态

- 日期：2026-08-08
- 分支：`agent/psrh-043-phase5-reliability`
- Worktree：`PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-reliability`
- 基线：`02e67aa`
- 当前 HEAD：`54a9dc9`
- 当前状态：PSRH-043 已收尾，暂停该分支修改，等待 PSRH-042 完成
- 集成状态：未集成到 `main`

## 工作范围与约束

本会话只处理 PSRH-043 阶段 5 的告警清理、资源/复位诊断、Host 测试和证据收尾。

明确保持不变的范围：

- 不修改 `firmware/main/matter_app.cpp` 或 `firmware/main/state_machine.c`；
- 不引入 PSRH-042 未提交代码；
- 不修改 watchdog、OTA、分区、复位、安全策略或离线行为；
- 不烧录、不执行硬件测试、不扩展阶段 6 内容；
- 可只读查看 PSRH-042 分支，但不修改其 Worktree。

构建要求使用 `-j2`，目标构建使用隔离目录和实际工具链路径：

```sh
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2
```

## 主要实现

### 有界健康诊断

在 `firmware/main/health_diag.c/.h` 增加了无动态分配的诊断路径：

- 当前空闲堆和历史最低空闲堆；
- 复位原因及 normal/software/panic/watchdog/brownout/unknown 分类；
- FreeRTOS 任务栈高水位及聚合最小值；
- 固定容量 32 个任务记录；
- 任务数、捕获数、容量和 `truncated` 状态写入启动/周期日志；
- 保留现有 `network` 任务的 30 秒诊断节奏。

审查问题已修复：

- 在调度器暂停期间复制 `TaskStatus_t.pcTaskName` 到模块自有固定缓冲区；
- 调度恢复后只使用复制后的任务名，不再保存任务控制块中的名称指针；
- `uxTaskGetSystemState()` 返回 0（任务数超过固定数组容量）时，保留真实任务数并显式记录截断；
- FreeRTOS 返回的栈高水位按 `StackType_t` words 转换为 bytes 后记录。

### 告警清理

- 删除 `network.c` 中无调用方的项目自有 `command_is_link_event()`，消除项目自有 unused warning；
- 保留 `firmware/CMakeLists.txt` 中移除 `-Wno-overloaded-virtual`、`-Wno-format-nonliteral`、`-Wno-format-security` 的有效改动；
- 未增加新的告警抑制；上游 ESP-Matter/ConnectedHomeIP 告警仍保持可见并写入证据。

### 收尾文档与契约

- `agent/tasks/PSRH-043.yml` 已提交，状态改为 `VERIFY_FAILED`；
- `firmware/README.md` 已恢复为基线内容，未保留当前电脑路径或 PSRH-043 专用构建目录；
- `firmware/CMakeLists.txt` 已改为路径无关的占位构建说明，无本机绝对路径或任务专用目录；
- 阶段 5 build、size、warning、resource、Host 和硬件延期证据已更新；
- handoff 最终提交列表包含 `ef9d728` 和本次核心收尾提交 `6e6f415`，并记录 handoff 更新提交 `54a9dc9`。

## 真实目标构建结果

目标配置和构建在 `firmware/` 下使用 `/home/administrator/esp` 工具链执行。

```sh
idf.py -B /tmp/psrh-043-phase5-reliability-build set-target esp32c6
idf.py -B /tmp/psrh-043-phase5-reliability-build reconfigure
ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all
idf.py -B /tmp/psrh-043-phase5-reliability-build size
```

结果：

- `set-target esp32c6`：exit 0；
- `reconfigure`：exit 0；GN 生成 5902 targets from 467 files；
- `ninja ... -j2 all`：exit 1，`[1466/1498]` 在只读的 PSRH-042 `firmware/main/matter_app.cpp` 失败；
- `idf.py ... size`：exit 2，因应用重复构建失败，未生成应用 ELF/map；
- 失败分类为 `VERIFY_FAILED`，不是环境 `BLOCKED`；
- bootloader 已生成：`0x5670` bytes，剩余 `0x2990` bytes（32%）；
- 应用 Flash/RAM size 尚未获得；
- 失败涉及 `StaticSupportedModesManager`、Matter callback designated initializer、`ModeOptionStruct` 等 API/type 错误，未在 PSRH-043 分支修复。

目标编译期间，PSRH-043 自有源文件未观察到项目自有编译告警。仍观察到的告警属于上游 ConnectedHomeIP/ESP-Matter 或现有 CMake/Kconfig 配置问题，包括 camera optional settings 的 `maybe-uninitialized`、Color Control 的 `direction` 可能未初始化，以及依赖兼容性/配置提示。

## Host 与差异验证

在 `tests/host` 执行：

```sh
make clean && make -j2 all
```

结果：exit 0，`-Wall -Wextra -Werror` 下 130/130 通过：

| Suite | 通过数 |
|---|---:|
| `occ_sm` | 17 |
| `ui_rgb` | 20 |
| `button_mode` | 10 |
| `env_alert_sm` | 21 |
| `night_window_sm` | 25 |
| `network_reconnect_sm` | 34 |
| `health_diag` | 3 |
| **总计** | **130** |

`git diff --check` 已通过。最终只读核验确认：

- 工作树干净；
- `firmware/README.md` 与基线一致；
- `firmware/CMakeLists.txt` 无 `/home/`、`/root/`、`/tmp/` 本机或任务路径；
- PSRH-042 的 `matter_app.cpp`、`state_machine.c` 与基线无差异；
- 未执行烧录或硬件测试。

## 提交链

| Commit | 内容 |
|---|---|
| `045f37f` | 增加有界阶段 5 健康诊断 |
| `2fe9d38` | 记录初始验证缺口 |
| `d0b5851` | 初始阶段 5 handoff |
| `9d83e4b` | 修复任务名生命周期审查问题并提交任务契约 |
| `ef9d728` | 更新真实构建、size、warning 和资源证据 |
| `6e6f415` | 恢复 README、清理 CMake 路径并将契约置为 `VERIFY_FAILED` |
| `54a9dc9` | 将收尾提交 SHA 回填 handoff |

## 后续边界

PSRH-043 当前不宣称阶段 5 PASS。待 PSRH-042 Builder 在其分支完成 Matter 修复并提交后，才可按既定顺序集成 PSRH-042、再集成 PSRH-043，并重新执行目标 build、size、启动资源采集和硬件验证。
