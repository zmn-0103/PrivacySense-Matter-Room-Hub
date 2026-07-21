# W01: Wi-Fi 重连验收测试

| 项 | 内容 |
|---|---|
| 固件版本 | privacy-sense-matter-room-hub v1.0 (ELF SHA256: 261375c71) |
| 硬件版本 | ESP32-C6 QFN40 rev v0.2 |
| 测试日期 | 2026-07-21 |
| 测试类型 | 硬件集成验收 |
| 证据来源 | 结果从串口终端人工摘录，完整原始日志未保存 |

## 前置条件

- 设备已烧录含 `network_diag` 控制台命令的固件（`wifi_cred`, `wifi_status`, `wifi_fault`）
- 历史 coredump 已从 flash 擦除（归档至 `coredump_20260720_prior_build.bin`）
- Wi-Fi AP: SSID=`<TEST_SSID>`, channel 1, 2.4 GHz

## 测试步骤与结果

### 1. 物理断电重启 — NVS 凭据持久化

电源复位（POWERON）后日志显示 `saved credentials found`，SM 自动进入 CONNECTING。

```
I (862) network: saved credentials found
I (1142) network: sm: event=0 -> state=1 attempts=0 auth_fail=0
```

**结果: PASS** ✅

### 2. 错误密码 — 3 次认证失败后 STOPPED

注入 `wifi_cred Zmn wrongpass`，观察到认证失败退避序列：

| 次数 | reason | 退避时间 | auth_fail |
|---|---|---|---|
| 1 | 15 (4WAY_HANDSHAKE_TIMEOUT) | 1 s | 1 |
| 2 | 205 (CONNECTION_FAIL) | 2 s | 1 |
| 3 | 15 | 4 s | 2 |
| 4 | 205 (CONNECTION_FAIL) | 8 s | 2 |
| 5 | 15 | — | 3 **→ STOPPED** |

```
E (267632) network: reconnect STOPPED
```

恢复正确密码后 STOPPED→CONNECTING→CONNECTED。

**结果: PASS** ✅

### 3. SNTP 时间同步

连接后约 30 秒内 `time_synced: yes`。

**结果: PASS** ✅

### 4a. RECONFIG_TIMEOUT

通过 `wifi_fault reconfig_block` 注入故障，在 RECONFIGURING 状态丢弃 DISCONNECTED 事件：

```
W (340752) network: fault: dropping DISCONNECTED in RECONFIGURING
W (345762) network: RECONFIGURING timeout (5000 ms)
I (345762) network: sm: event=6 -> state=1 attempts=0 auth_fail=0
```

5 秒超时后自动恢复 CONNECTING→CONNECTED。

**结果: PASS** ✅

### 4b. NVS 写入失败重试

通过 `wifi_fault nvs_fail` 注入故障，`write_pending_credentials()` 返回 CRED_WRITE_RETRYABLE：

```
W (520502) network: fault: injecting NVS write failure
I (521502) network: credentials written to NVS
```

故障自动清除后重试成功。

**结果: PASS** ✅

### 4c. 队列/FIFO 溢出恢复

通过 `wifi_fault queue_storm` 直接向环形缓冲区注入 40 条 `NET_CMD_WIFI_DISCONNECTED` 命令（绕过 credential mailbox 单事务限制），超过 32 槽深度。

```
W (42671) network: queue storm: injected 40 cmds, overrun_count 0 -> 7
```

- 溢出 7 次（`ingress_overruns=7`，该计数器读取后清零），spill slot 机制正常工作
- 无崩溃、无 watchdog 复位
- 后续 `wifi_status` 显示 `ingress_overruns=0` 是因前次读取已将计数器清零，并非凭据注入主动复位；系统恢复后 `state=CONNECTED`

**结果: PASS** ✅

### 5. Coredump 处理

- Flash 中历史 coredump 已保存至 `tests/evidence/coredump_20260720_prior_build.bin`（65536-byte 分区镜像，内部有效 dump 约 5412 bytes）
  - SHA-256: `73eb38d564dccc332c55a48c061e5e4fc57c306c13ac0e58513407285743ceb0`
  - 外部保存位置: `/home/projects/PrivacySense-Matter-Room-Hub/tests/evidence/`（该二进制文件未加入 Git，需另行备份）
- SHA 不匹配当前 ELF（来自之前构建），已从 flash 擦除
- 本轮测试未产生新的 coredump

**结果: PASS** ✅

## 汇总

| # | 用例 | 结果 |
|---|---|---|
| 1 | 物理断电重启 NVS 持久化 | ✅ |
| 2 | 错误密码 3 次 → STOPPED | ✅ |
| 3 | SNTP 同步 | ✅ |
| 4a | RECONFIG_TIMEOUT | ✅ |
| 4b | NVS 写入失败重试 | ✅ |
| 4c | 队列/FIFO 溢出恢复 | ✅ |
| 5 | Coredump 清理 | ✅ |

**总体结论: PASS** — 所有验收用例通过，Wi-Fi 重连路径全部覆盖。
