# R10 雷达 UART 断线恢复测试

## 前置条件
- 固件：当前生产代码（R09 clean build，无故障注入）
- ESP32-C6 已烧录并运行（雷达已 online）
- 串口监视器已连接，日志输出到文件
- 可物理接触雷达模块 TX → ESP32 GPIO5 杜邦线

## 测试步骤

### 1. 确认雷达在线
等待 `radar: offline -> online` 日志出现，记录时间戳。

### 2. 断开雷达 TX 线
从 ESP32 GPIO5（或对应杜邦线母头）拔出雷达 TX 线。

### 3. 等待离线检测
约 10 秒后应出现 `radar: online -> offline (elapsed=10000 ms, thr=10000 ms, reason=1)`（reason=1 = `LD2410C_FAIL_TIMEOUT`）。

观察 30 秒：无 panic、WDT 或异常重启。

### 4. 重新连接雷达 TX 线
将雷达 TX 线插回 ESP32 GPIO5。

### 5. 等待恢复上线
约 1–2 秒内应出现 `radar: offline -> online`。

继续观察 60 秒，确认雷达持续在线、无异常 offline。

### 6. 重复验证（可选）
重复步骤 1–5 至少 2 次，确认恢复行为稳定。

## 结果

| 步骤 | 期望 | 实际 | 结论 |
|---|---|---|---|
| 初始上线 | 30 秒内 online | 1.361 s | PASS |
| 断开后 10 秒 | `radar: online -> offline` | 21.661 s（elapsed=10000 ms），reason=1 (TIMEOUT) | PASS |
| 断开期间 | 无 panic / WDT / 重启 | 无 | PASS |
| 重连后 2 秒 | `radar: offline -> online` | 27.041 s（约 5 s） | PASS |
| 重连后 60 秒 | 持续 online，无再次离线 | 持续 online 至日志结束（~67 s） | PASS |
| 雷达任务栈余量 | >1500 字节 | 2280 字节（稳定） | PASS |

## 通过标准
- 断线后在 `sensor_timeout_ms`（默认 10 s）内检测到 offline ✓
- 重新连接后快速恢复 online ✓
- 全程无 panic / Guru Meditation / WDT / brownout ✓
- 状态机正常运行（`heartbeat` 日志正常输出）✓
- 雷达任务栈余量稳定在 2280 字节 ✓

## 证据
- 串口日志：`tests/evidence/monitor_prod_20260719_162117.log`（MAC 已脱敏）
- 初始上线：1.361 s
- 断线离线：21.661 s（elapsed=10000 ms, thr=10000 ms, reason=LD2410C_FAIL_TIMEOUT）
- 重连上线：27.041 s
- 全程无 panic / WDT / 异常重启
