# 开发环境搭建流程

## 1. 目标与当前状态

本文档同时记录 ESP32-C6 + ESP-Matter 环境的重建流程和当前验收状态。2026-07-17 的证据表明核心工具链已经可用，可以开始固件代码；未完成项单独列出，不再沿用“环境尚未安装”的旧结论。

2026-07-17 的已保存证据：

| 项目 | 当前状态 | 说明 |
|---|---|---|
| WSL2 / Linux | 已安装 | 默认发行版 `Ubuntu-24.04`，系统为 Ubuntu 24.04.4 LTS，WSL VERSION=2 |
| VS Code | 已安装 | 版本 `1.123.0`；WSL 远程扩展状态仍待人工确认，不阻塞终端开发 |
| Git for Windows | 已安装 | 版本 `2.53.0.windows.2` |
| `usbipd-win` / USB | 已可用 | WSL 中已出现 `/dev/ttyUSB0`，CP2102 链路可识别开发板 |
| ESP-IDF / `idf.py` | 已安装 | `/root/esp/esp-idf`，`idf.py --version` 为 v5.4.1 |
| ESP-Matter | 已安装 | `/root/esp/esp-matter`；实际分支和 commit 尚未记录 |
| 官方 light 示例 | 已有构建产物 | target=`esp32c6`；`light.bin` 已记录 SHA256，尚未完成本轮烧录/commissioning 验收 |
| ESP32-C6 实机 | 已识别 | QFN40 revision v0.2，Flash 已确认 16 MB |
| Windows Python/CMake/Ninja | 存在其他工程版本 | 不复用；以 ESP-IDF 在 WSL2 内安装的版本为准 |

## 2. 冻结版本

| 组件 | 项目基线 | 选择理由 |
|---|---|---|
| WSL | WSL2 | ESP-Matter 官方支持的 Windows 开发方式 |
| Linux | Ubuntu 24.04.4 LTS | 以用户实际安装和证据为准；ESP-Matter 官方主机说明接受 Ubuntu 24.04 LTS |
| ESP-IDF | `v5.4.1` | 与 `esp-matter release/v1.5` 的分支说明匹配 |
| ESP-Matter | `release/v1.5` 目标基线 | 已安装；实际分支/commit 待记录前不得声称已完成版本锁定 |
| Target | `esp32c6` | 与已购 ESP32-C6-DevKitC-1-N16 一致 |
| IDE | VS Code + Remote - WSL | 编辑、终端和工具链都运行在同一个 Linux 环境 |

禁止只升级 ESP-IDF 或只升级 ESP-Matter。升级必须重新核对两者兼容关系，并重新构建、烧录和 commissioning。

官方依据：

- [ESP-Matter Windows/WSL 开发说明](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html)
- [ESP-Matter release/v1.5 分支说明](https://github.com/espressif/esp-matter/tree/release/v1.5)
- [ESP32-C6-DevKitC-1 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh-cn/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)
- [Microsoft WSL2 连接 USB 设备](https://learn.microsoft.com/zh-cn/windows/wsl/connect-usb)

## 3. 目录约定

ESP-IDF、ESP-Matter 及其大量子模块必须放在 WSL2 的 Linux 文件系统中，不能放到 `/mnt/f`：

```text
/root/esp/esp-idf
/root/esp/esp-matter
/home/projects/PrivacySense-Matter-Room-Hub   # 实际 WSL 项目目录
firmware/build                                # 默认构建目录（idf.py build）
```

当前 Windows 主仓库仍位于：

```text
F:\MyProject\PrivacySense-Matter-Room-Hub
```

Windows 主目录和 WSL 构建副本不能同时编辑。当前仓库尚未形成可用于双端同步的提交基线时，按 [开发流程](development-workflow.md) 使用不带 `--delete` 的单向 rsync；建立 Git 基线后再切换到 Git 同步。

## 4. 阶段 A：安装 WSL2 和 Ubuntu

以下命令在管理员 PowerShell 中执行，会修改系统配置；执行前应确认已经保存其他工作。

先查看可安装发行版：

```powershell
wsl --list --online
```

如需重建环境，安装 Ubuntu 24.04：

```powershell
wsl --install -d Ubuntu-24.04
```

如果系统提示重启，完成重启后首次打开 Ubuntu，并创建 Linux 用户名和密码。随后在 PowerShell 中验证：

```powershell
wsl --update
wsl --status
wsl --list --verbose
```

验收标准：Ubuntu 的 `VERSION` 为 `2`，且进入 Ubuntu 后 `uname -a` 显示的内核版本不低于 `5.10.60.1`。

## 5. 阶段 B：安装 VS Code 扩展

在 Windows VS Code 中安装：

- `Remote - WSL`
- `ESP-IDF`
- `C/C++`

打开 Ubuntu 终端，在 Linux 目录中执行：

```bash
code .
```

确认 VS Code 左下角显示 `WSL: Ubuntu-24.04`。ESP-IDF 扩展应安装到 WSL 远程环境，而不是只安装在 Windows 本地。

## 6. 阶段 C：安装 Linux 基础依赖

以下命令在 Ubuntu 24.04 中执行：

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0
```

不要把 Windows 中已有的 Python、CMake、Ninja 路径加入 WSL 的 `PATH`。

## 7. 阶段 D：安装 ESP-IDF v5.4.1

在 Ubuntu 中执行：

```bash
mkdir -p /root/esp
cd /root/esp
git clone --recursive --branch v5.4.1 \
  https://github.com/espressif/esp-idf.git
cd /root/esp/esp-idf
./install.sh esp32c6
```

每个新终端都需要加载环境：

```bash
source /root/esp/esp-idf/export.sh
idf.py --version
```

验收标准：`idf.py --version` 输出 `ESP-IDF v5.4.1`，且没有引用 Windows Python 路径。

## 8. 阶段 E：安装 ESP-Matter release/v1.5

ESP-Matter 包含较多子模块和主机工具，下载和首次构建耗时较长。以下命令在 Ubuntu 中执行：

```bash
source /root/esp/esp-idf/export.sh
cd /root/esp
git clone --recursive --branch release/v1.5 \
  https://github.com/espressif/esp-matter.git
cd /root/esp/esp-matter
./install.sh
```

每个新终端继续加载：

```bash
source /root/esp/esp-idf/export.sh
source /root/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1
```

安装成功后记录实际 commit，后续填回本文档：

```bash
git -C /root/esp/esp-idf rev-parse HEAD
git -C /root/esp/esp-matter rev-parse HEAD
git -C /root/esp/esp-matter/connectedhomeip/connectedhomeip rev-parse HEAD
```

| 仓库 | 已验证 commit | 安装时间 | 验证方式 |
|---|---|---|---|
| ESP-IDF | `[TODO: 未记录，不阻塞开始编码]` | 2026-07-17 前 | `idf.py --version` 已输出 `ESP-IDF v5.4.1` |
| ESP-Matter | `[TODO: 未记录，不阻塞开始编码]` | 2026-07-17 前 | `/root/esp/esp-matter` 已存在并可导出环境 |
| connectedhomeip | `[TODO: 未记录，不阻塞开始编码]` | 2026-07-17 前 | `chip-tool` 已存在；最终由 esp-matter 子模块 commit 固定 |

> 首次构建摘要（构建命令、Flash/RAM 实际占用、任务栈高水位、上游 warning、隐私信息处理说明）统一记录在 [firmware/README.md](../firmware/README.md) "首次构建状态记录"节。Builder AI 完成首次构建后必须同时刷新本表与该节。

## 9. 阶段 F：构建官方 ESP32-C6 Matter 示例

先构建 ESP-Matter 自带的 `light` 示例验证工具链，不要用仍含 TODO 的项目固件骨架反推工具链是否正确：

```bash
source /root/esp/esp-idf/export.sh
source /root/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1

cd /root/esp/esp-matter/examples/light
idf.py set-target esp32c6
idf.py build
```

验收标准：构建完成且没有 error；构建摘要显示 target 为 `esp32c6`。

## 10. 阶段 G：把开发板 USB 挂载到 WSL2

开发板有两个 USB-C 接口。首次验证推荐使用板载 USB-to-UART 接口；必须使用支持数据传输的 USB 线。

保持 Ubuntu 窗口开启，在管理员 PowerShell 中执行：

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

其中 `<BUSID>` 替换为 CP210x 或 Espressif USB 设备对应的总线编号。`bind` 通常只需执行一次；重新插拔或重启后可能需要再次执行 `attach`。

在 Ubuntu 中验证：

```bash
lsusb
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

如当前用户没有串口权限：

```bash
sudo usermod -aG dialout "$USER"
```

执行后退出并重新进入 Ubuntu，使用户组生效。

## 11. 阶段 H：烧录、串口和 Matter 验证

假设设备节点为 `/dev/ttyUSB0`：

```bash
cd /root/esp/esp-matter/examples/light
idf.py -p /dev/ttyUSB0 flash monitor
```

如果使用原生 USB Serial/JTAG 接口，设备节点可能是 `/dev/ttyACM0`，以实际枚举结果为准。退出 monitor 使用 `Ctrl+]`。

验收标准：

- 固件能够烧录并启动；
- 串口日志没有持续重启、brownout 或 flash size 错误；
- 日志显示 BLE commissioning 已开启；
- 能使用 `chip-tool` 或一个实际 Matter 控制器完成一次配网；
- 重启设备后仍能重新上线。

## 12. 阶段 I：验证 N16 Flash

卖家图片标注开发板为 `ESP32-C6-DevKitC-1-N16`，但官方标准 DevKitC-1 文档主要描述 8 MB 版本，因此不能只依据商品标题。2026-07-17 已通过芯片读取确认本机开发板为 16 MB SPI Flash；后续更换开发板时仍须重新实测。

```bash
esptool.py --chip esp32c6 -p /dev/ttyUSB0 flash_id
```

当前结果：检测容量为 16 MB，项目 `sdkconfig.defaults` 已按 16 MB Flash 配置。每次更换开发板后若实测容量不一致，必须先修正文档和分区设计，再继续构建或烧录。

## 13. 项目固件构建方式

项目已存在 `firmware/CMakeLists.txt`。Builder AI 完成第一批阻塞代码修改后，在 WSL2 项目副本中执行：

```bash
source /root/esp/esp-idf/export.sh
source /root/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1

cd /home/projects/PrivacySense-Matter-Room-Hub/firmware
idf.py set-target esp32c6
idf.py build
```

首次成功构建后，Builder AI 必须提交：

- `sdkconfig.defaults`
- `partitions.csv`
- `dependencies.lock`（如使用 IDF Component Manager）
- ESP-IDF、ESP-Matter 和 connectedhomeip 的实际 commit
- 构建命令、Flash/RAM 摘要和已知警告

## 14. 常见错误

| 现象 | 优先检查 |
|---|---|
| `idf.py: command not found` | 是否执行 `source /root/esp/esp-idf/export.sh` |
| ESP-Matter Python 包缺失 | 是否执行 `source /root/esp/esp-matter/export.sh` |
| WSL 看不到开发板 | PowerShell 中是否完成 `usbipd attach`，USB 线是否支持数据 |
| 串口权限错误 | 用户是否加入 `dialout`，是否重新登录 WSL |
| 构建异常缓慢 | SDK 是否错误地放在 `/mnt/f`，是否启用 `IDF_CCACHE_ENABLE=1` |
| Flash size 不一致 | 运行 `esptool.py --chip esp32c6 --port /dev/ttyUSB0 flash_id`，不要只相信商品名称 |
| Matter 代码与 IDF API 不兼容 | 是否混用了 `release/v1.5` 与非 `v5.4.1` 的 ESP-IDF |
| `idf.py` 出现 `urllib3.contrib.appengine` 导入错误 | 当前 shell 是否额外激活了 connectedhomeip Pigweed venv；关闭该终端，在新终端只加载 ESP-IDF/ESP-Matter export 后重试。`chip-tool` 使用独立终端或绝对路径 |

## 15. 完成清单

- [x] Ubuntu 24.04.4 已安装为 WSL2
- [ ] VS Code 可以进入 `WSL: Ubuntu-24.04`（终端开发不依赖此项）
- [x] ESP-IDF v5.4.1 已安装并可执行 `idf.py`
- [x] ESP-Matter 已安装（分支/commit 待记录）
- [ ] 三个仓库的 commit 已记录
- [x] 官方 `light` 示例已有可核验构建产物
- [x] ESP32-C6 USB 已挂载到 WSL2 并识别为 `/dev/ttyUSB0`
- [ ] 在未激活 Pigweed venv 的干净 shell 中复核 `idf.py size`
- [ ] 官方示例烧录和串口输出正常
- [ ] Matter commissioning 至少成功一次
- [x] N16 的 16 MB Flash 已通过 `flash_id` 验证

## 16. 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 0.1 | 2026-07-16 | 初版：记录当前环境缺口及从 WSL2 到 Matter commissioning 的完整搭建流程 |
| 0.2 | 2026-07-17 | 以实际 Ubuntu 24.04.4 和已安装工具链为准；统一 `/home/projects` 路径；记录 16 MB Flash、已完成项和剩余非阻塞项 |
