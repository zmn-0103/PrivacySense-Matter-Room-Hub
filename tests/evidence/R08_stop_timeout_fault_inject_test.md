# R08 停止超时负向测试

## 前置条件
- 固件编译时启用 `LD2410C_FAULT_INJECT=ON`（首次停止请求时延迟 1100 ms）
- ESP32-C6 已烧录该测试固件
- 串口监视器已连接
- 雷达已连接并在线
- **注意：** 启动时如出现 core dump，是前一次失败测试遗留的 crash，不是本轮新崩溃。确认本次启动日志中无 `Guru Meditation` / `Instruction access fault` 即可。

## 编译方式

```bash
cd /home/projects/PrivacySense-Matter-Room-Hub/firmware
. /root/esp/esp-idf/export.sh
. /root/esp/esp-matter/export.sh

idf.py -B build-r08 reconfigure build \
    -DLD2410C_FAULT_INJECT=ON
```

### 构建后三重确认（全部通过才能烧录）
```bash
# 1. CMake cache 已启用
rg '^LD2410C_FAULT_INJECT:BOOL=ON$' build-r08/CMakeCache.txt

# 2. 编译命令包含宏
rg 'LD2410C_TEST_FAULT_INJECT_STOP=1' build-r08/compile_commands.json

# 3. ELF 包含故障注入字符串
strings build-r08/privacy-sense-matter-room-hub.elf | rg 'FAULT INJECT'
```

### 烧录
```bash
idf.py -B build-r08 -p /dev/ttyUSB0 flash monitor 2>&1 | \
    tee /tmp/monitor_r08_$(date +%Y%m%d_%H%M%S).log
```
**注意：** 不要使用普通 `build/` 目录的 .bin，必须用 `build-r08/`。

## 测试步骤

### 1. 启动并确认雷达在线
观察日志：`radar: offline -> online`

### 2. 短按 BOOT 键（<1 秒快速按一下松开）

### 3. 观察完整日志序列
预期输出：
```
R08: === begin ===
R08: calling ld2410c_stop()
W ld2410c: FAULT INJECT: sleeping 1100 ms to force timeout
E ld2410c: stop timeout — radar task did not exit in time (call stop() again to retry)
R08: ld2410c_stop() returned ESP_ERR_TIMEOUT
R08: timeout confirmed
R08: start() during STOPPING returned ESP_ERR_INVALID_STATE
R08: PASS — start() correctly rejected
R08: retrying stop()
R08: retry stop() returned ESP_OK
R08: calling start()
R08: start() returned ESP_OK
R08: PASS — first valid frame received after restart
R08: === done ===
```

## 期望结果
| 步骤 | 期望 | 实际 | 结论 |
|---|---|---|---|---|
| stop 超时 | 返回 ESP_ERR_TIMEOUT | ESP_ERR_TIMEOUT | PASS |
| start 被阻止 | 返回 ESP_ERR_INVALID_STATE | ESP_ERR_INVALID_STATE | PASS |
| 重试 stop | 首次超时后的重试停止成功（ESP_OK） | ESP_OK | PASS |
| start 恢复 | 返回 ESP_OK | ESP_OK | PASS |
| 首帧恢复 | 10 秒内收到有效帧 | ~560 ms | PASS |

## 通过标准
- stop() 超时路径正确返回 ESP_ERR_TIMEOUT
- 超时后 UART 不被提前删除（任务仍在运行）
- 超时后 start() 被正确阻止（返回 ESP_ERR_INVALID_STATE）
- 重试 stop() 能完成清理（返回 ESP_OK）
- 再次 start() 成功恢复（返回 ESP_OK）
- 重启后 10 秒内收到第一帧有效雷达数据
- 之后雷达持续在线，不再次离线

## 证据
- 串口日志：`tests/evidence/monitor_r08_20260719_154344.log`（MAC 已脱敏）
- 完整通过 R08 所有步骤：stop 超时 → start 被拒 → 重试 stop 成功 → start 恢复 → 首帧 ~560 ms
- 无 panic / WDT / 资源泄漏
- 日志中 `Found core dump 13828 bytes` 是前次测试遗留的 crash，在本轮 R08 测试中未新增崩溃
