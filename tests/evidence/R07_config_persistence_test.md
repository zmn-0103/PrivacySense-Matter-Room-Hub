# R07 配置断电保存测试

## 前置条件
- ESP32-C6 已烧录最新固件（含 config 模块）
- 串口监视器已连接，可观察日志
- 雷达已连接并在线

## 测试步骤

### 1. 读取当前配置基准值
在串口监视器中观察启动日志，确认 `config re-read ok (sensor_timeout_ms=10000)` 出现。
或通过日志确认 `heartbeat` 中的 config re-read 输出。

**记录：** sensor_timeout_ms = ______

### 2. 修改配置
通过 config_task 修改 `sensor_timeout_ms` 为一个明显不同的值（如 15000）。
方式：在 `config_set()` 调用路径中 enqueue 一个 `CONFIG_EVENT_APPLY_NEW`。

修改后日志应出现：
```
config re-read ok (sensor_timeout_ms=15000)
```

### 3. 确认 NVS 已写入
config_task 的设计是"先写 NVS，再更新 RAM"。如果 `config_set()` 返回 `ESP_OK`，表示已入队。
等待 config_result_queue 返回 `CONFIG_RESULT_OK`。

### 4. 硬复位
按 RESET 键或断电重启。

### 5. 读回配置
启动后，观察日志：
```
config re-read ok (sensor_timeout_ms=15000)
```
确认值与修改后一致。

### 6. 恢复基准值
将 `sensor_timeout_ms` 改回 10000。
等待 `CONFIG_RESULT_OK`。
再次硬复位。

### 7. 确认恢复
启动日志应显示 `sensor_timeout_ms=10000`。

## 期望结果
| 步骤 | 期望 | 实际 | 结论 |
|---|---|---|---|
| 2. 修改后 | config re-read 显示 15000 | | |
| 5. 重启后读回 | sensor_timeout_ms=15000 | | |
| 7. 恢复后重启 | sensor_timeout_ms=10000 | | |

## 通过标准
- 修改后的值在断电重启后保持不变
- 恢复后的值在断电重启后保持不变
- NVS `ps_cfg` 分区未损坏（不触发 erase+reinit）

## 证据
- 串口日志（启动 + config re-read 行）
- 如有 NVS dump 工具，可附上 NVS blob 内容对比
