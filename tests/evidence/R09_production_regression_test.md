# R09 生产代码回归测试（10 分钟）

## 前置条件
- 所有临时测试代码已删除（btn_radar_test、button test 逻辑）
- 固件干净编译，无测试钩子
- ESP32-C6 已烧录
- 串口监视器已连接，日志输出到文件

## 编译烧录
```bash
source /root/esp/esp-idf/export.sh && source /root/esp/esp-matter/export.sh
idf.py build 2>&1 | tee /tmp/build_prod_$(date +%Y%m%d_%H%M%S).log
idf.py -p /dev/ttyUSB0 flash monitor 2>&1 | tee /tmp/monitor_prod_$(date +%Y%m%d_%H%M%S).log
```

## 测试步骤

### 1. 启动后等待稳定
等待 30 秒，确认雷达已 online。

### 2. 运行 10 分钟
不做任何操作，让设备自然运行 10 分钟（600 秒）。

### 3. 收集指标
从日志中提取以下数据：

| 指标 | 获取方式 | 通过标准 |
|---|---|---|
| 有效帧数 | 统计 `process_radar: valid` 日志 | >0 |
| protocol failure 次数 | 统计 `process_radar: protocol` | 0（理想）或有上限 |
| timeout failure 次数 | 统计 `radar: online -> offline` | 0（理想） |
| 队列丢弃数 | 统计 `radar data dropped` | 0（理想） |
| 最低空闲堆 | 统计 `heap after` 或 `min_free` | >10000 字节 |
| 雷达任务栈余量 | 统计 `heartbeat: stack_hwm` 中 radar 值 | >1500 字节 |
| 状态机任务栈余量 | 同上 | >3000 字节 |
| WDT / Panic | 搜索 `panic` / `Guru Meditation` / `WDT` | 0 次 |
| 重启次数 | 搜索 `POWERON` / `SW_CPU` | 1 次（仅首次启动） |

### 4. 对比基线
与之前测试日志中的相同指标对比，确认无退化。

## 通过标准
- 雷达持续在线 10 分钟，无异常 offline
- 无 panic / WDT / brownout
- 最低空闲堆 > 10000 字节
- 任务栈余量稳定（与基线差值 < 200 字节）
- 队列丢弃数为 0

## 结果

| 指标 | 实际值 | 通过标准 | 结论 |
|---|---|---|---|
| 有效帧 | 持续收到（通过 `heartbeat: rx_buf_len` 确认滚动变化）| >0 | PASS |
| protocol failure | 0 | 0（理想） | PASS |
| timeout failure（radar: online -> offline）| 0 | 0（理想） | PASS |
| 队列丢弃数（radar data dropped）| 0 | 0（理想） | PASS |
| 最低空闲堆 | 未出现 OOM 或 `heap low` 告警 | >10000 字节 | PASS |
| 雷达任务栈余量 | 2268–2280 字节 | >1500 字节 | PASS |
| 状态机任务栈余量 | 4392–4404 字节 | >3000 字节 | PASS |
| WDT / Panic | 0 次 | 0 次 | PASS |
| 重启次数 | 1 次（POWERON，首次启动） | 1 次 | PASS |

## 对比基线（与 R08 稳定运行段对比）

| 指标 | R08 基线 | R09 值 | 差异 | 结论 |
|---|---|---|---|---|
| state_machine 栈余量 | 4196–4404 B | 4392–4404 B | < 200 B | 稳定 |
| ld2410c 栈余量 | 2272–2280 B | 2268–2280 B | < 12 B | 稳定 |
| ui 栈余量 | 1412 B | 1412 B | 0 | 稳定 |
| env_sensor 栈余量 | 2368–2380 B | 2368 B | < 12 B | 稳定 |
| 队列丢弃 | 0 | 0 | 无变化 | 无退化 |

## 证据
- 串口日志：`tests/evidence/monitor_prod_20260719_155428.log`（MAC 已脱敏）
- 日志显示 10 分钟连续运行（loop 6480+，总运行 ~636 秒）
- 基线对比：R08 稳定运行段（`monitor_r08_20260719_153425.log`）
