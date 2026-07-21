# T07: DHT22 传感器异常 — RANGE 五次阈值 & Smoke Test

- **固件版本**: 7e322fa (feat: DHT22 parser, env sensor online state, Wi-Fi credentials guard)
- **硬件**: ESP32-C6-DevKitC-1-N16 + AM2302 (DHT22) on GPIO2
- **日期**: 2026-07-19

## 前置条件

DHT22 以 5 s 周期采样，状态机 `process_env()` 已实现双计数器：
- PROTOCOL/TIMEOUT → 3 次连续 → `env_sensor_online=false`
- RANGE → 5 次连续 → `env_sensor_online=false`
- 任何有效采样 → 清零双计数器，设 `env_sensor_online=true`

## 步骤

1. 启动设备，等待首次采样
2. 等待状态 `offline → online`
3. 注入 5 次 RANGE 失败（通过临时 TEST 块将有效采样的 `failure` 覆写为 `ENV_SENSOR_FAIL_RANGE`）
4. 观察第 5 次 RANGE 触发 `online → offline`
5. 停止注入，等待恢复 `offline → online`
6. 继续运行 60+ s 确认无后续离线

## 期望结果

| 时间点 | 事件 | 期望状态 |
|--------|------|----------|
| ~0.9 s | 首次采样 OK | `offline → online` |
| RANGE #1–4 | 连续 RANGE 失败 | 保持 `online` |
| RANGE #5 | 达到 5 次阈值 | `online → offline` (count=5, thr=5, reason=3) |
| 恢复后首帧有效 | 有效采样 | `offline → online` |
| 之后 60+ s | 持续正常采样 | `online`，fail=0 |

## 实际结果

```
0.906s   正常样本：offline -> online
5.906s   RANGE #1：保持 online
11.176s  RANGE #2：保持 online
16.196s  RANGE #3：保持 online
21.226s  RANGE #4：保持 online
26.246s  RANGE #5：online -> offline (count=5, thr=5, reason=3)
31.276s  恢复有效样本：offline -> online
```

之后连续正常运行至 ~112 s，环境任务 `fail=0`。无 panic、WDT、队列或互斥锁错误。任务栈余量约 2380 B。

## 生产 Smoke Test（TEST 块删除后）

- 运行约 81 s，17/17 采样均成功
- 温度 28.6–28.7°C，湿度 70.8–71.3%RH
- 启动后正常完成 `offline → online`，无再次离线
- 无 TEMP、TEST:、panic、WDT 日志
- 正式量程 -40～80°C

## 结论

**PASS** — RANGE 五次离线阈值、恢复上线、长期稳定采样均验证通过。

## 未覆盖用例

- 真正的 RMT TIMEOUT 恢复分支
- 负温度解码
- 混合失败计数（4 RANGE + 1 PROTOCOL 不应立即离线）
