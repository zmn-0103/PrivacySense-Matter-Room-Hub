# Hardware

此目录保存硬件连接表、BOM、模块数据手册索引、装配照片和外壳资料。

首版使用开发板和传感器模块，不要求自绘 PCB。连接表必须先于固件中的 GPIO 定义冻结。

## 文件清单

| 文件 | 说明 |
|---|---|
| [connection-table.md](connection-table.md) | GPIO/UART/I2C/供电连接表（冻结） |
| [BOM.csv](BOM.csv) | 冻结 BOM（含型号、电流、电压） |

> 连接表中的 GPIO 分配有待实物到货后验证确认项（见连接表第 7 节）。验证通过后更新本表并通知所有开发方。
