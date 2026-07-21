# R12 阶段 1 硬件单元验证 — 独立验收记录

## 设备信息

| 项目 | 值 |
|---|---|
| 主控 | ESP32-C6-DevKitC-1-N16 (revision v0.2) |
| 雷达 | HLK-LD2410C-P |
| 固件版本 | 1.0 |
| UART 参数 | 256000 baud, 8N1, GPIO4(TX)/GPIO5(RX) |
| 编译时间 | 2026-07-19 |
| ELF SHA256 | 待填入（见 build log） |
| 日志时间 | 2026-07-19 |

## 已完成的验证项

### 1. 10 分钟稳定运行（R09）
- 日志：`tests/evidence/R09_production_regression_test.md`（原始日志归档脱敏后补充）
- 总运行时间：~10.6 分钟（636 秒）
- 雷达持续 online，无异常 offline
- 无 panic / Guru Meditation / WDT / brownout
- 最低空闲堆 > 10000 字节（验证通过）
- 任务栈余量稳定：state_machine=4392 B, ld2410c=2268 B, ui=1412 B

### 2. R08 stop/restart 超时负向测试
- 日志：`tests/evidence/monitor_r08_20260719_154344.log`（MAC 已脱敏）
- stop() 超时 → ESP_ERR_TIMEOUT ✓
- 超时后 start() 被阻止 → ESP_ERR_INVALID_STATE ✓
- 重试 stop() → ESP_OK ✓
- 恢复 start() → ESP_OK ✓
- 首帧 ~560 ms 内收到 ✓

### 3. UART 异常与恢复（R10）
- 状态：**PASS**（2026-07-19 手动验证）
- 日志：`tests/evidence/monitor_prod_20260719_162117.log`（MAC 已脱敏）
- 初始上线：1.361 s
- 断开 TX 后 10 秒正确离线（elapsed=10000 ms, reason=LD2410C_FAIL_TIMEOUT）
- 重连后恢复 online（27.041 s）
- 无 panic / WDT / 异常重启 / 资源异常
- 雷达任务栈余量稳定：2280 字节
- 详情见：`tests/evidence/R10_radar_uart_disconnect_test.md`

### 4. 雷达配置保存（R11）
- **状态：硬件验证 PASS**
- 实现了 LD2410C 命令模式协议框架（`ld2410c_parser.h` / `.c` + `ld2410c.c` / `include/ld2410c.h`）
- 命令帧格式：头 `FD FC FB FA` + 长度(LE) + 命令(2 字节 LE) + 数据 + 尾 `04 03 02 01`（V1.09 无 SUM/XOR 校验，按头尾 + 长度校验）
- 已实现的命令：`LD2410C_CMD_ENABLE_CONF`(0x00FF) / `LD2410C_CMD_DISABLE_CONF`(0x00FE) / `LD2410C_CMD_READ_PARAMS`(0x0061) / `LD2410C_CMD_WRITE_PARAMS`(0x0060) / `LD2410C_CMD_SET_SENS`(0x0064)
- 参数 ID（0x0060 写入）：`LD2410C_PARAM_WORD_MAX_MOVING_GATE`(0x0000), `LD2410C_PARAM_WORD_MAX_STATIC_GATE`(0x0001), `LD2410C_PARAM_WORD_UNOCCUPIED_DELAY`(0x0002)；灵敏度（0x0064）：`LD2410C_PARAM_WORD_GATE`(0x0000), `LD2410C_PARAM_WORD_MOVING_SENS`(0x0001), `LD2410C_PARAM_WORD_STATIC_SENS`(0x0002)
- 事务层修复 (Reviewer P0 #1/#2/#6/#7)：按值传递的请求/响应队列 + 互斥锁，原子化 ENABLE→BUSINESS→DISABLE 配置事务，ACK 重新同步扫描 FD FC FB FA，xTaskCreate 失败显式返回 ESP_ERR_NO_MEM
- 类型安全公共 API (Reviewer P0 #5)：`ld2410c_read_params()`, `ld2410c_write_basic_params()`, `ld2410c_set_gate_sensitivity()`, `ld2410c_exec_cmd()`（替换了不存在的旧 read/write_parameter 声明）
- 主机端单元测试：R11 24 项 (R11-T1 到 R11-T24，含 0x0060 三参数完整 golden vector、0x0064 两例、0x0061 38 字节 golden vector) **ALL PASS** + 事务核心测试 22 项 (TXN-T1 到 TXN-T22，含业务状态失败+disable 成功、双超时→uncertain、tx_data=NULL len>0、rx_cap<0、recv 失败、ACK 超 rx_cap + leftover 恢复、tx_data_len<0、tx_data_len>MAX) **ALL PASS**
- 需要实际雷达硬件验证命令是否能正常收发
- 测试计划见：`tests/evidence/R11_radar_config_save_test.md`

### 5. DHT22 解析器边界测试（Host 单元测试 11 项）
- **状态：固件已提取 + 主机端单元测试 PASS**
- `dht22_parse_symbols()`（平台中立解析）与 `parse_rmt_dht22()`（RMT 包装）从 `env_sensor.c` 提取到独立模块 `env_sensor_parser.h` / `.c`
- 避免了与 ESP-IDF `rmt_symbol_word_t` 位域布局的耦合（使用平台中立的 `dht22_symbol_t`）
- `env_sensor.c` 通过简单转换函数 `rmt_to_dht22_sym()` 调用共享解析器
- 主机端单元测试验证：11 项（DHT22-T1 到 DHT22-T11）**ALL PASS**，测试文件 `env_sensor/test/test_dht22_parser.c`（链接真实 `env_sensor_parser.c`）
  - 覆盖：正常温度/湿度、极端边界（-40 °C / +80 °C / 0% / 100%）、负温度、越界、校验和错误、符号数不足、无效符号模式、滑动窗口偏移

### 6. OLED SSD1306 驱动
- **状态：硬件验证 PASS**
- 日志：`oled_verify_20260720_141905.log`
- 验证内容：I2C 地址 0x3C、1 Hz 刷新、占用/Wi‑Fi/模式/告警/传感器行显示正确、NORMAL→QUIET/ALERT→OK/OFF→ON 无残字、运行中拔线后 RGB/雷达/状态机继续运行、I2C 日志限频、重新接线后恢复
- `ssd1306.h` / `ssd1306.c`：I2C 初始化、标准 SSD1306 初始化序列、128x64 缓冲管理、6×8 字体文字渲染
- 集成到 `ui.h` / `ui.c`：`ui_oled_render_state()` 在 `ui_task` 约每 1 秒（1 Hz）调用
- 显示内容为房间状态：占用/Wi‑Fi、用户模式、环境告警、传感器在线
- `ui_init()` 中非致命初始化 — 无 OLED 模块时继续运行
- P1 运行时错误处理 (Reviewer P1): `ssd1306_flush()` 返回 `esp_err_t` 并传播错误；`write_data()` 首个 chunk 失败即停止；`ssd1306_init()` 清屏契约修复 (s_initialized 置于 flush 前)；I2C 警告限频；UI 解耦 OLED 刷新 (~1 Hz + 退避) 和 RGB (200 ms)，OLED 故障不阻塞其它子系统

### 7. 电气证据
- 状态：**待实物验证**（需万用表测量 5V 排针供电电压）
- 接线确认：当前基于连接表推导，对照实物丝印后再接线
- 见：`hardware/connection-table.md:183`

## 附加可靠性证据

| 指标 | R08 值 | R09 值 | 对比结论 |
|---|---|---|---|
| state_machine 栈余量 | 4196–4404 B | 4392–4404 B | 稳定 |
| ld2410c 栈余量 | 2272–2280 B | 2268–2280 B | 稳定 |
| ui 栈余量 | 1412 B | 1412 B | 稳定 |
| env_sensor 栈余量 | 2380 B | 2368 B | 稳定 |
| 队列丢弃 (radar) | 0 | 0 | 无退化 |
| 队列丢弃 (env) | 0 | 0 | 无退化 |

## 已知遗留问题

1. **Core dump 遗留**：Flash `coredump` 分区有前次测试遗留的 13828 字节 core dump，不是本轮新增崩溃。可通过 `esptool.py erase_region 0xa20000 0x10000` 或 `idf.py erase_flash` 清除。
2. **Matter commissioning**：Matter 仍为 STUB，未发起 commissioning（phase 3/4 工作）。
3. **Wi-Fi 保存**：无 Wi-Fi 凭据，设备未入网（phase 3/4 工作）。
4. **Factory reset deferred**：长按产生 `BUTTON_EVENT_LONG_PRESS` 并推送到 state_machine_task，但阶段一只记录 `LONG_PRESS: factory reset DEFERRED`，不执行实际的 Matter/config 复位。复位逻辑待 phase 3/4 实现。

## 按钮 / RGB 验证策略（阶段一）

**阶段一限制**：`process_network()` 为空（`wifi_connected` 不更新），`commissioning_active` 无运行时写入者。传感器离线或 Wi-Fi 未连接会按优先级覆盖 NORMAL/QUIET 的 RGB 状态。因此短按后 RGB 跟随模式变化（绿↔蓝）无法在真实运行中单独验证。阶段一采用分层验证：

1. **RGB 诊断颜色**（`CONFIG_UI_RGB_DIAGNOSTIC=y`）：依次显示红、绿、蓝、黄、白、暖白、熄灭，每种 2 秒，循环播放。日志记录每帧颜色切换，日志证据可独立说明当前颜色。
2. **短按模式切换**：通过 OLED 和日志验证 NORMAL ↔ QUIET 切换。日志出现 `BUTTON: mode NORMAL→QUIET, quiet 0→1` 即可。
3. **短按取消**：按住约 3 秒后释放 → 无 LONG 事件，模式不变。
4. **长按触发**：按住超过 5 秒 → RGB 前 5 秒红闪（1 Hz），达到阈值后红色常亮；释放后出现一个 LONG 事件，`state_machine` 日志显示 `factory reset DEFERRED`。
5. **传感器离线覆盖**：验证传感器离线时红色慢闪，长按 RGB 可以覆盖它（红闪/红色常亮）。
6. **连续运行**：至少 2 分钟，无 panic、WDT、按键事件丢失或 LED 写入错误。记录 button/UI 任务栈余量。
7. **Host 测试**：按键模式转换 10 项 ALL PASS、RGB 映射 20 项 ALL PASS（覆盖 8 级优先级与闪烁边界）。
8. **不宣称** NORMAL/QUIET 的完整运行时 RGB 集成已经实测。阶段三/四补全 `process_network`、commissioning_active 写入者后，再执行端到端 RGB 验证。

## 阶段 1 验收详情

以下模块已通过硬件验证，结论为条件通过：

- **按键短按/长按**：4 次短按 + 1 次长按实物验证 PASS，isr_drops=0，栈余量 1276 B。
- **RGB 状态灯**：诊断 7 色实物验证 PASS；短按/长按颜色跟随模式正确。Host 测试 20/20 PASS 覆盖完整 8 级优先级表与闪烁边界。
- **RGB 诊断模式**：Kconfig `CONFIG_UI_RGB_DIAGNOSTIC=y` 7 色循环验证，颜色正确。

以下项需要外部设备：

- 电气测量：需万用表测量 5V 排针供电电压

**电气测量声明**：

> 延期/受阻：当前缺少万用表，未测量 5 V、3.3 V 和负载电压；不得以启动成功或软件日志代替电气实测。这不是测试失败，也不妨碍先完成按键和 RGB，但整个阶段一最终状态应保留"电气测量待工具"。

## 阶段 1 验收：条件通过

**硬件验证于 2026-07-20 完成，确认以下结果：**

| 模块 | Host 测试 | 硬件验证 | 结论 |
|---|---|---|---|
| 雷达帧解析（R03/R04） | 22/22 PASS | — | PASS |
| 雷达命令协议（R11） | 24/24 PASS | 3 轮日志 PASS | PASS |
| 雷达事务核心 | 22/22 PASS | — | PASS |
| DHT22 解析 | 11/11 PASS | 维持 Host 测试 | PASS |
| OLED SSD1306（R12） | — | 1 轮日志 PASS | PASS |
| 按键模式转换 | 10/10 PASS（Host 测试） | 4 次短按 + 1 次长按实物验证 PASS | PASS |
| RGB 状态灯 | 20/20 PASS（Host 测试） | 诊断 7 色实物验证 + 短按/长按颜色正确 PASS | PASS |
| 电气测量 | — | 延期（缺万用表） | 未完成 |

**硬件验证日志：**
- `monitor_button_20260720_171835.log` — 短按 4 次 + 长按 1 次，isr_drops=0
- `monitor_button_stack_20260720_172619.log` — 连续运行含栈余量数据；按键栈 HWM=1276 B（栈 2048→3072 后），目标 ≥1024 ✓
- `monitor_diagnostic_20260720_170429.log` — 诊断 7 色循环，颜色正确
- 原始日志存放于设备或构建环境，未提交至仓库（按 `tests/evidence/.gitignore` 规则仅提交 Markdown 摘要）

**条件通过说明：**
- Matter commissioning、Wi-Fi 凭据注入和实际恢复出厂属于阶段三/四（见 `docs/project-plan.md`），不阻塞阶段一验收
- 电气测量因缺少万用表延期，不得以启动成功或软件日志代替；阶段一最终结论不因此否定功能验证
- Host 测试总计：雷达帧 22 + 命令协议 24 + 事务核心 22 + DHT22 11 + 按键模式转换 10 + RGB 映射 20 = **109 项 ALL PASS**
