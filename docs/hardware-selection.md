# 硬件选型清单

## 1. 首版推荐配置

| 模块 | 推荐型号 | 用途 | 备注 |
|---|---|---|---|
| 主控 | ESP32-C6-DevKitC-1-N16 | Wi-Fi、BLE、Matter over Wi-Fi | 已购；已通过 `flash_id` 确认 ESP32-C6、芯片 revision v0.2 和 16 MB Flash；板级丝印版本待记录 |
| 毫米波雷达 | HLK-LD2410C-P 单模块 | 检测有人、无人和持续存在 | 已购；5 V 供电、供电能力 > 200 mA、3.3 V IO、UART 默认 256000 baud |
| 温湿度 | AM2302/DHT22 | 环境状态 | 已有；3.3 V 供电，单总线 DATA，最小读取间隔 2 s，首版按 5 s 周期采样 |
| CO2（可选） | SCD40/SCD41 | 环境舒适度提醒 | 首版可不采购 |
| 状态灯 | DevKitC-1 板载可寻址 RGB LED | 显示状态 | 已包含在开发板上，由 GPIO8 驱动，无需另购首版 RGB 模块 |
| 显示屏 | SSD1306 OLED | 显示网络、状态、故障 | 已有；参考代码的 `0x78` 是 8-bit 地址字节，ESP-IDF 使用 7-bit 地址 `0x3C` |
| 明暗检测（可选） | LM393 光敏电阻模块 | 后续真实明暗判定 | 已有；首版继续使用时段 NIGHT，模块只作后续扩展，不输出标定 lux 值 |
| 交互 | 轻触按键或旋转编码器 | 切换模式、恢复配网 | 首版优先按键 |
| 供电 | 5 V USB-C 电源模块 | 设备供电 | 只使用低压 USB |
| 连接与外壳 | 面包板、洞洞板、排针、端子、3D 打印外壳 | 原型装配 | 雷达前方不能被金属遮挡 |

主控、雷达、DHT22 和 OLED 已经具备，现有模块足以开始首版代码和面包板验证。无需再购买 BME280/SHT31 或 BH1750；CO2、外壳和备用传感器后置。仍需确认支持数据传输的 USB-C 线、面包板、杜邦线以及 DHT22 DATA 上拉电阻是否齐备。

装配前确认模块工作电压、UART/I2C/单总线电平、雷达排针方向、OLED I2C 地址和板载上拉，以及 DHT22 是否已集成 DATA 上拉。首版所有外设均使用 3.3 V IO，只有 LD2410C-P 的 VCC 使用 5 V。

## 2. 不会画 PCB 的三种解决方案

### 方案 A：开发板 + 传感器模块 + 面包板/洞洞板

ESP32-C6、LD2410C-P、后续温湿度模块和显示模块都使用现成开发板，按连接表用杜邦线连接，稳定后焊到洞洞板并放入 3D 打印外壳。

优点是零 PCB 设计成本、改线快、适合学习和面试演示；缺点是线缆多、抗干扰和外观一般，不适合量产。首版推荐此方案。

### 方案 B：现成扩展板或模块化载板

使用 ESP32-C6 开发板，再购买带端子、排针或 Grove/Qwiic 接口的传感器扩展板和现成载板，通过端子连接电源、I2C、UART 和 GPIO。

优点是比面包板可靠整洁，不需要掌握 PCB 软件；缺点是接口和尺寸受现成产品限制，必须核对电压、电平、引脚和机械尺寸。

### 方案 C：委托 PCB 工程师按连接表设计载板

自己确定功能、模块型号、GPIO 分配、供电需求和 BOM，再将连接表交给硬件工程师或设计服务商，委托设计一块承载开发板和传感器接口的低复杂度载板，并输出原理图、PCB、BOM、Gerber 和装配图。

你不需要独立画 PCB，但必须审查电源树、地线、UART 电平、I2C 上拉、连接器方向、雷达安装区域和 USB 保护。不要让 AI 直接生成 Gerber 后不经硬件审查就下单。

## 3. 推荐路线

采用 `方案 A -> 方案 B -> 方案 C`：先完成原型闭环，再整理样机，最后在接口和外壳稳定后委托设计载板。

## 4. 原型连接约束

> 冻结后的精确 GPIO、UART、I2C 和供电连接见 [连接表](../hardware/connection-table.md) 和 [BOM.csv](../hardware/BOM.csv)。以下为概要。

```text
ESP32-C6 UART1  <->  LD2410C-P (GPIO 4 TX / GPIO 5 RX)
ESP32-C6 GPIO 3  <-   LD2410C-P OUT（可选状态冗余输入）
ESP32-C6 GPIO 2 <->  AM2302/DHT22 DATA（上拉至 3.3 V）
ESP32-C6 I2C    <->  SSD1306 OLED / SCD40（可选）（GPIO 6 SDA / GPIO 7 SCL）
ESP32-C6 GPIO 10 <-  光敏模块 DO（可选扩展，首版不装）
ESP32-C6 GPIO 9 <->  按键（DevKitC-1 Boot 按钮）
ESP32-C6 GPIO 8 ->   开发板板载 RGB LED
5 V USB         ->   开发板；开发板 5V 排针 -> LD2410C-P VCC
GND             ->   所有模块共地
```

GPIO16/17 与板载 USB-to-UART 桥接器关联，保留给下载和日志，不再用于雷达。具体 GPIO 分配、电平、上拉和电流估算以 [连接表](../hardware/connection-table.md) 为准，不能在固件中复制一份未经确认的引脚定义。

## 5. 已购资料依据

- `F:\MyProject\PrivacySense-Matter-Room-Hub资料\HLK LD2410C生命存在感应模组说明书V1.09.pdf`
- `F:\MyProject\PrivacySense-Matter-Room-Hub资料\LD2410C 串口通信协议 V1.09.pdf`
- `F:\MyProject\stm32-iot-getewa资料\【telesky旗舰店】AM2302（DHT22） 温湿度传感器通用\AM2302（DHT22） 温湿度传感器通用\AM2302（DHT22）数据手册.pdf`
- `F:\MyProject\stm32-iot-getewa资料\OLED\OLED.c`（仅参考 SSD1306 命令和字库，不移植 STM32 软件 I2C）
- `F:\BaiduNetdiskDownload\STM32入门教程资料\模块资料\4-光敏电阻传感器\光敏电阻模块使用说明.png`
- 商品图片：`ESP32-C6-DevKitC-1-N16（焊接）`、`LD2410C-P单模块`
- [ESP32-C6-DevKitC-1 官方用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh-cn/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)

资料目录中的 `ESP32-WROOM-32` 和 Arduino 1.8.19 教程属于旧款 ESP32 开发板资料，不适用于本项目的 ESP32-C6、RISC-V 和 ESP-Matter 工具链，不能作为 GPIO 或环境搭建依据。
