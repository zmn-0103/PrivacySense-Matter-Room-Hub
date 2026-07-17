# 开发流程：编译烧录命令文档

## 1. 文档目标

本文档记录 Windows 主机开发时的完整工作流，包括：
- Windows ↔ WSL 代码同步（rsync）
- 编译、烧录、监视日志命令
- 日常开发快速参考

## 2. 环境约定

| 项目 | 路径/值 |
|---|---|
| Windows 项目根目录 | `F:\MyProject\PrivacySense-Matter-Room-Hub` |
| WSL 项目副本路径 | `/home/projects/PrivacySense-Matter-Room-Hub` |
| WSL 发行版 | `Ubuntu-24.04` |
| ESP-IDF 路径 | `/root/esp/esp-idf` |
| ESP-Matter 路径 | `/root/esp/esp-matter` |
| 构建输出目录 | `/root/build/privacy-sense` |
| 目标芯片 | `esp32c6` |
| 串口设备 | `/dev/ttyUSB0`（根据实际调整） |

> **注意**：WSL 路径在 Windows 文件管理器中访问时使用 `\\wsl.localhost\Ubuntu-24.04\home\projects\PrivacySense-Matter-Room-Hub`

## 3. Windows → WSL 代码同步

### 3.1 前置条件

推荐直接使用 WSL 内的 rsync；不要求在 Windows PowerShell 中额外安装 rsync。

先执行 `command -v rsync`；仅在命令不存在时，才在 WSL Ubuntu 中安装：
```bash
sudo apt update && sudo apt install -y rsync
```

### 3.2 同步命令

#### 从 Windows 同步到 WSL（开发常用）

在 **WSL 终端** 中执行：

```bash
rsync -avh --progress \
  --exclude='.git/' \
  --exclude='build/' \
  --exclude='sdkconfig' \
  --exclude='*.pyc' \
  --exclude='__pycache__/' \
  /mnt/f/MyProject/PrivacySense-Matter-Room-Hub/ \
  /home/projects/PrivacySense-Matter-Room-Hub/
```

> **路径说明**：
> - `/mnt/f/...` 是 WSL 对 Windows F: 盘的挂载路径。
> - `/home/projects/...` 是 WSL Linux 文件系统中的项目副本，不是 UNC 路径。
> - 末尾的 `/` 很重要：源目录末尾 `/` 表示同步目录内容，不带 `/` 表示同步目录本身


#### 从 WSL 同步回 Windows（提交审查时）

在 **WSL 终端** 中执行：

```bash
# 示例：只同步本轮明确修改的单个源文件；按实际文件名重复执行
rsync -avh \
  /home/projects/PrivacySense-Matter-Room-Hub/firmware/main/[TODO_CHANGED_FILE] \
  /mnt/f/MyProject/PrivacySense-Matter-Room-Hub/firmware/main/
```

不要在命令中使用 `--delete`，不要反向同步 `sdkconfig`、`build/` 或 `/root/build/privacy-sense`。开始反向同步前停止 Windows 侧编辑；同步文件清单应与 Builder AI 的交付清单一致。

### 3.3 排除规则

同步时应排除的文件和目录（使用 `--exclude` 参数）：

```bash
rsync -avh --progress \
  --exclude='.git/' \
  --exclude='build/' \
  --exclude='sdkconfig' \
  --exclude='*.pyc' \
  --exclude='__pycache__/' \
  --exclude='.vscode/' \
  /mnt/f/MyProject/PrivacySense-Matter-Room-Hub/ \
  /home/projects/PrivacySense-Matter-Room-Hub/
```

排除规则说明：
```text
.git/              # Git 仓库（WSL 应有独立 clone）
build/             # 构建产物
sdkconfig          # 本地配置（由 sdkconfig.defaults 生成）
*.pyc              # Python 缓存
__pycache__/       # Python 缓存
.vscode/           # IDE 配置（如不同步 IDE 设置）
```

### 3.4 替代方案：Git 工作流

建立首个本地提交基线和远程仓库后，长期推荐使用 Git 管理代码。当前所有文件尚未跟踪时先使用单向 rsync，避免两份目录互相覆盖：

```powershell
# Windows: 在用户明确决定提交范围后提交
git add .
git commit -m "feat: description"

# WSL: 在已配置同一远程仓库后拉取
cd /home/projects/PrivacySense-Matter-Room-Hub
git pull
```

> `git push` 仍需用户明确授权，本流程不默认执行。Git 工作流可避免手动同步错误；rsync 只用于当前快速原型阶段的 Windows → WSL 单向同步。

## 4. 编译烧录流程

### 4.1 加载环境（每个新终端必须执行）

在 **WSL 终端** 中执行：

```bash
# 加载 ESP-IDF 环境
source /root/esp/esp-idf/export.sh

# 加载 ESP-Matter 环境
source /root/esp/esp-matter/export.sh

# 启用 CCache 加速编译
export IDF_CCACHE_ENABLE=1
```

> **提示**：可将上述命令添加到 `~/.bashrc` 或创建别名：
> ```bash
> alias load-esp='source /root/esp/esp-idf/export.sh && source /root/esp/esp-matter/export.sh && export IDF_CCACHE_ENABLE=1'
> ```

> **Python 环境隔离**：用于 `idf.py` 的终端不要再执行 `connectedhomeip/.environment/activate.sh`。Pigweed/`chip-tool` 使用另一个终端或直接调用 `/root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool`，避免 Pigweed 的 `urllib3` 覆盖 ESP-IDF Python 环境。

### 4.2 首次配置

```bash
cd /home/projects/PrivacySense-Matter-Room-Hub/firmware

# 设置目标芯片（只需执行一次）
idf.py -B /root/build/privacy-sense set-target esp32c6

# 应用 sdkconfig.defaults 并生成 sdkconfig
idf.py -B /root/build/privacy-sense reconfigure
```

### 4.3 编译

```bash
# 进入固件目录
cd /home/projects/PrivacySense-Matter-Room-Hub/firmware

# 完整编译
idf.py -B /root/build/privacy-sense build

# 或只编译修改的部分（增量编译，更快）
idf.py -B /root/build/privacy-sense build
```

**编译成功标志**：
```
Project build complete. To flash, run this command:
idf.py -p /dev/ttyUSB0 flash
```

### 4.4 烧录

```bash
# 烧录到开发板
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 flash

# 烧录并立即监视日志
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 flash monitor
```

> **串口设备说明**：
> - `/dev/ttyUSB0`：USB-to-UART 转换器（如 CP210x、CH340）
> - `/dev/ttyACM0`：USB Serial/JTAG（ESP32-C6 原生 USB）
> - 使用 `ls /dev/tty*` 查看可用串口
> - 如权限不足：`sudo usermod -a -G dialout $USER`（需重新登录）

### 4.5 监视日志

```bash
# 只监视日志（不烧录）
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 monitor

# 退出监视：Ctrl + ]
```

### 4.6 完整一键命令

```bash
# 加载环境 + 编译 + 烧录 + 监视（可保存为脚本）
source /root/esp/esp-idf/export.sh && \
source /root/esp/esp-matter/export.sh && \
export IDF_CCACHE_ENABLE=1 && \
cd /home/projects/PrivacySense-Matter-Room-Hub/firmware && \
idf.py -B /root/build/privacy-sense build && \
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 flash monitor
```

## 5. 常用开发命令

### 5.1 配置菜单

```bash
# 打开交互式配置菜单
idf.py -B /root/build/privacy-sense menuconfig
```

### 5.2 清理构建

```bash
# 清理构建产物（保留 sdkconfig）
idf.py -B /root/build/privacy-sense clean

# 完全清理（包括 sdkconfig，需重新 reconfigure）
idf.py -B /root/build/privacy-sense fullclean
```

### 5.3 查看分区表

```bash
# 检查分区表配置
idf.py -B /root/build/privacy-sense partition-table
```

### 5.4 查看 Flash 信息

```bash
# 读取 Flash 容量（验证开发板规格）
esptool.py --chip esp32c6 --port /dev/ttyUSB0 flash_id
```

### 5.5 保存串口日志

```bash
# 监视并保存日志到文件
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 monitor | tee /root/build/privacy-sense/serial.log
```

## 6. USB 设备挂载（WSL2）

### 6.1 挂载开发板到 WSL

在 **管理员 PowerShell** 中执行：

```powershell
# 查看 USB 设备列表
usbipd list

# 绑定设备（只需执行一次，记录 BUSID）
usbipd bind --busid <BUSID>

# 挂载到 WSL
usbipd attach --wsl --busid <BUSID>
```

### 6.2 验证设备

在 **WSL 终端** 中验证：

```bash
# 查看 USB 设备
lsusb

# 查看串口设备
ls -l /dev/ttyUSB*
ls -l /dev/ttyACM*

# 查看内核日志（确认设备识别）
dmesg | tail -20
```

### 6.3 自动挂载规则（可选）

创建 `/etc/udev/rules.d/99-esp-devices.rules`：

```text
# ESP32 USB-to-UART
SUBSYSTEM=="usb", ATTR{idVendor}=="10c4", ATTR{idProduct}=="ea60", GROUP="dialout", MODE="0660"
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="7523", GROUP="dialout", MODE="0660"

# ESP32 USB Serial/JTAG
SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="1001", GROUP="dialout", MODE="0660"
```

应用规则：
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## 7. 日常开发工作流

### 7.1 典型开发循环

```text
1. 先确定本轮唯一编辑源：推荐 VS Code Remote - WSL 打开 `/home/projects/PrivacySense-Matter-Room-Hub`
   ↓
2. 在 WSL 项目目录编辑代码；Windows F: 目录本轮只作审查镜像，不并行修改
   ↓
3. WSL 编译（idf.py build）
   ↓
4. 烧录到开发板（idf.py flash）
   ↓
5. 监视日志验证（idf.py monitor）
   ↓
6. 需要 Codex 审查时，停止 WSL 侧编辑，再把明确的源码/文档同步回 Windows；如有问题，返回步骤 1
```

### 7.2 快速迭代脚本

创建 `build-flash.sh` 在 WSL 中：

```bash
#!/bin/bash
# build-flash.sh - 快速编译烧录脚本

set -e

echo "=== 加载环境 ==="
source /root/esp/esp-idf/export.sh
source /root/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1

echo "=== 编译 ==="
cd /home/projects/PrivacySense-Matter-Room-Hub/firmware
idf.py -B /root/build/privacy-sense build

echo "=== 烧录 ==="
idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 flash

echo "=== 完成 ==="
echo "运行 'idf.py -B /root/build/privacy-sense -p /dev/ttyUSB0 monitor' 查看日志"
```

使用方式：
```bash
chmod +x /home/projects/PrivacySense-Matter-Room-Hub/tools/build-flash.sh
/home/projects/PrivacySense-Matter-Room-Hub/tools/build-flash.sh
```

## 8. 故障排查

### 8.1 编译失败

| 问题 | 解决 |
|---|---|
| `idf.py: command not found` | 确认已执行 `export.sh` |
| 子模块缺失 | `cd /root/esp/esp-matter && git submodule update --init --recursive` |
| 内存不足 | 关闭其他应用，或增加 WSL 内存（`.wslconfig`） |
| CMake 错误 | `idf.py fullclean` 后重新 `reconfigure` |

### 8.2 烧录失败

| 问题 | 解决 |
|---|---|
| `Failed to open port` | 确认 USB 设备已挂载到 WSL，检查 `/dev/ttyUSB0` 是否存在 |
| 权限拒绝 | `sudo usermod -a -G dialout $USER`，重新登录 |
| 芯片未响应 | 按住 Boot 按钮，点击 Reset，再执行烧录 |
| Flash 写入错误 | 检查 Flash 容量配置，执行 `esptool.py --chip esp32c6 --port /dev/ttyUSB0 flash_id` |

### 8.3 rsync 同步问题

| 问题 | 解决 |
|---|---|
| 路径找不到 | 确认 WSL 发行版名称正确（`wsl --list`） |
| 权限错误 | 使用 `sudo rsync` 或检查文件权限 |
| 同步慢 | 添加 `--exclude` 排除大目录，或使用 Git |

## 9. 版本记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-07-16 | v1.0 | 初始版本，记录 rsync 同步和编译烧录流程 |
| 2026-07-17 | v1.1 | 统一实际 `/home/projects` 路径；修正 rsync 执行环境、Git/push 边界、Pigweed 与 ESP-IDF Python 隔离说明 |

## 10. 参考文档

- [开发环境搭建流程](development-environment.md)
- [项目计划书](project-plan.md)
- [固件 README](../firmware/README.md)
- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
- [ESP-Matter ESP32-C6 官方文档](https://docs.espressif.com/projects/esp-matter/en/release-v1.4.2/esp32c6/developing.html)
