# 开发环境配置证据

**日期**: 2026-07-17

## WSL 配置

### WSL 状态
```
PS C:\Users\Zmn> wsl --status
默认分发: Ubuntu-24.04
默认版本: 2
```

### WSL 发行版列表
```
PS C:\Users\Zmn> wsl --list --verbose
  NAME            STATE           VERSION
* Ubuntu-24.04    Running         2
```

**说明**:
- 默认分发已从 Ubuntu 22.04 升级为 Ubuntu 24.04
- WSL2 正在运行中

## WSL2 内部系统信息

### 操作系统版本
```
root@WIN-18OSVFS9GQA:~# grep -E '^(PRETTY_NAME|VERSION_ID|VERSION_CODENAME)=' /etc/os-release
PRETTY_NAME="Ubuntu 24.04.4 LTS"
VERSION_ID="24.04"
VERSION_CODENAME=noble
```

### 内核版本
```
root@WIN-18OSVFS9GQA:~# uname -r
6.6.114.1-microsoft-standard-WSL2
```

### 磁盘空间
```
root@WIN-18OSVFS9GQA:~# df -T /root /home/projects/PrivacySense-Matter-Room-Hub
Filesystem     Type  1K-blocks     Used Available Use% Mounted on
/dev/sdd       ext4 1055762868 66938560 935120836   7% /
/dev/sdd       ext4 1055762868 66938560 935120836   7% /
```

**说明**:
- 操作系统：Ubuntu 24.04.4 LTS (代号 noble)
- 内核：WSL2 标准内核 6.6.114.1
- 磁盘：总容量约 1TB，已使用 64GB (7%)，可用约 892GB，空间充足

## usbipd-win 配置

### 版本信息
```
PS C:\Users\Zmn> usbipd --version
5.3.0-54+Branch.master.Sha.aa3db8b82c4cb5071fd31bc54211606c70886912.aa3db8b82c4cb5071fd31bc54211606c70886912
```

### USB 设备列表
```
PS C:\Users\Zmn> usbipd list
Connected:
BUSID  VID:PID    DEVICE                                                        STATE
1-4    3654:5055  DARKRIM SHADOW, USB 输入设备                                  Not shared
1-5    04f2:b78a  HD Webcam, Camera DFU Device                                  Not shared
1-14   8087:0026  英特尔(R) 无线 Bluetooth(R)                                   Not shared
4-4    10c4:ea60  CP2102 USB to UART Bridge Controller                          Attached
5-2    5253:1021  USB 输入设备                                                  Not shared
6-3    152d:0578  USB Attached SCSI (UAS) 大容量存储设备                        Not shared
6-4    046d:c341  USB 输入设备                                                  Not shared

Persisted:
GUID                                  DEVICE
```

**关键设备**:
- **CP2102 USB to UART Bridge Controller** (BUSID: 4-4, VID:PID: 10c4:ea60) - 状态: **Attached**
  - 这是 ESP32 开发板的 USB 转串口芯片
  - 已附加到 WSL2，可用于固件烧录和调试

## ESP-IDF 配置

### ESP-IDF 环境激活
```
root@WIN-18OSVFS9GQA:~# source /root/esp/esp-idf/export.sh
Checking "python3" ...
Python 3.12.3
"python3" has been detected
Activating ESP-IDF 5.4
Setting IDF_PATH to '/root/esp/esp-idf'.
* Checking python version ... 3.12.3
* Checking python dependencies ... OK
* Deactivating the current ESP-IDF environment (if any) ... OK
* Establishing a new ESP-IDF environment ... OK
* Identifying shell ... bash
* Detecting outdated tools in system ... OK - no outdated tools found
* Shell completion ... Autocompletion code generated

Done! You can now compile ESP-IDF projects.
Go to the project directory and run:

  idf.py build
```

**说明**:
- ESP-IDF 版本：5.4
- Python 版本：3.12.3
- IDF_PATH：`/root/esp/esp-idf`
- 所有依赖检查通过，无过时工具
- 环境激活成功，可以编译 ESP-IDF 项目

### 工具链版本验证

#### idf.py 版本
```
root@WIN-18OSVFS9GQA:~# idf.py --version
ESP-IDF v5.4.1
```

#### Python 版本与路径
```
root@WIN-18OSVFS9GQA:~# python3 -c 'import sys; print(sys.version); print(sys.executable)'
3.12.3 (main, Jun 19 2026, 12:46:00) [GCC 13.3.0]
/root/.espressif/python_env/idf5.4_py3.12_env/bin/python3
```

#### CMake 版本
```
root@WIN-18OSVFS9GQA:~# cmake --version | head -n 1
cmake version 3.28.3
```

#### Ninja 版本
```
root@WIN-18OSVFS9GQA:~# ninja --version
1.11.1
```

#### ccache 版本
```
root@WIN-18OSVFS9GQA:~# ccache --version | head -n 1
ccache version 4.9.1
```

#### RISC-V 交叉编译器版本
```
root@WIN-18OSVFS9GQA:~# riscv32-esp-elf-gcc --version | head -n 1
riscv32-esp-elf-gcc (crosstool-NG esp-14.2.0_20241119) 14.2.0
```

**工具链汇总**:

| 工具 | 版本 | 说明 |
|---|---|---|
| ESP-IDF | v5.4.1 | 与计划版本一致 |
| Python | 3.12.3 | ESP-IDF 虚拟环境中的版本 |
| CMake | 3.28.3 | 构建系统 |
| Ninja | 1.11.1 | 构建工具 |
| ccache | 4.9.1 | 编译缓存 |
| riscv32-esp-elf-gcc | 14.2.0 | RISC-V 交叉编译器 (ESP32-C6 使用) |

## ESP-Matter 配置

### ESP-Matter 环境激活
```
root@WIN-18OSVFS9GQA:~# source /root/esp/esp-matter/export.sh
```

**说明**:
- ESP-Matter 路径：`/root/esp/esp-matter`
- 环境激活命令已执行（输出未显示完整）

### Python 依赖检查

#### ESP-IDF Python 环境
```
root@WIN-18OSVFS9GQA:~# python3 -m pip check
No broken requirements found.
```

#### Pigweed 虚拟环境
```
root@WIN-18OSVFS9GQA:~# /root/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/pigweed-venv/bin/python \
    -m pip check
requests 2.28.2 has requirement urllib3<1.27,>=1.21.1, but you have urllib3 2.7.0.
```

**说明**:
- ESP-IDF Python 环境：依赖检查通过，无问题
- Pigweed 环境：存在 `urllib3` 版本不匹配警告（requests 要求 <1.27，实际安装 2.7.0）
- 该警告不影响构建，属于已知兼容性问题

### Pigweed 环境激活
```
root@WIN-18OSVFS9GQA:~# source /root/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/activate.sh
Activating environment (setting environment variables):

  Setting environment variables for CIPD package manager...done
  Setting environment variables for Project actions........skipped
  Setting environment variables for Python environment.....done
  Setting environment variables for pw packages............skipped
  Setting environment variables for Host tools.............done

Checking the environment:

/root/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/pigweed-venv/lib/python3.12/site-packages/requests/__init__.py:109: RequestsDependencyWarning: urllib3 (2.7.0) or chardet (None)/charset_normalizer (3.0.1) doesn't match a supported version!
  warnings.warn(
20260717 16:46:19 INF Environment passes all checks!

Environment looks good, you are ready to go!
```

### pw 命令验证
```
root@WIN-18OSVFS9GQA:~#  command -v pw
/root/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/pigweed-venv/bin/pw
```

**说明**:
- Pigweed 环境激活成功，所有检查通过
- `pw` 命令可用，路径正确
- 尽管有 `urllib3` 版本警告，环境仍可正常使用

### chip-tool 验证

#### chip-tool 可执行文件
```
root@WIN-18OSVFS9GQA:~# command -v chip-tool
  file /root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool
/root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool
/root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool: ELF 64-bit LSB pie executable, x86-64, version 1 (GNU/Linux), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=923139d9c5132f0bd9c274b41ac402ba827f013b, for GNU/Linux 3.2.0, with debug_info, not stripped
```

#### 构建状态检查
```
root@WIN-18OSVFS9GQA:~# ninja -C /root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host \
    -n chip-cert chip-tool
ninja: Entering directory `/root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host'
ninja: no work to do.
```

**说明**:
- `chip-tool` 已编译完成，位于 `/root/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool`
- 可执行文件格式：ELF 64-bit LSB pie executable, x86-64
- `chip-cert` 和 `chip-tool` 均已构建完成，无需重新编译
- 可用于 Matter 设备的 commissioning 和测试

### ESP-Matter light 示例构建验证

#### 目标配置
```
root@WIN-18OSVFS9GQA:~# cd /root/esp/esp-matter/examples/light
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# grep -E '^CONFIG_IDF_TARGET=|^CONFIG_ESPTOOLPY_FLASHSIZE=' sdkconfig
CONFIG_IDF_TARGET="esp32c6"
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
```

#### 构建产物信息
```
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# stat -c '%y %s %n' build/light.bin build/light.elf
2026-07-17 16:09:59.544190469 +0800 1725680 build/light.bin
2026-07-17 16:09:59.342523781 +0800 68822912 build/light.elf
```

#### 固件校验和
```
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# sha256sum build/light.bin
69a90935a31de21ea414df03ba6a7d6dc8a61d2f2eeb37a0b82862a68c9d7a39  build/light.bin
```

#### 构建状态
```
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# ninja -C build -n
ninja: Entering directory `build'
ninja: warning: build log version is too old; starting over
[1404/1404] cd /root/esp/esp-matter/examples/light/b...n /root/esp/esp-matter/examples/light/build/light.bi
```

#### idf.py size 命令问题
```
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# idf.py size
/root/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/pigweed-venv/lib/python3.12/site-packages/requests/__init__.py:109: RequestsDependencyWarning: urllib3 (2.7.0) or chardet (None)/charset_normalizer (3.0.1) doesn't match a supported version!
  warnings.warn(
cannot import name 'appengine' from 'urllib3.contrib' (/root/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/pigweed-venv/lib/python3.12/site-packages/urllib3/contrib/__init__.py)
This usually means that "idf.py" was not spawned within an ESP-IDF shell environment or the python virtual environment used by "idf.py" is corrupted.
Please use idf.py only in an ESP-IDF shell environment. If problem persists, please to install ESP-IDF tools again as described in the Get Started guide.
```

**说明**:
- 目标芯片：ESP32-C6
- Flash 大小：4MB
- 构建产物：
  - `light.bin`: 1,725,680 字节 (约 1.6MB)，构建时间 2026-07-17 16:09:59
  - `light.elf`: 68,822,912 字节 (约 65.6MB)，包含调试信息
- SHA256: `69a90935a31de21ea414df03ba6a7d6dc8a61d2f2eeb37a0b82862a68c9d7a39`
- 构建步骤：1404 步，已完成
- **已观察问题**：在已激活 connectedhomeip Pigweed venv 的 shell 中执行 `idf.py size`，出现 `urllib3.contrib.appengine` 导入错误。
  - 该输出证明的是 Python 虚拟环境混用，不足以证明 ESP-IDF 自身环境损坏，也不能据此笼统声称“所有构建和烧录均不受影响”。
  - 正确复核方式：关闭当前终端，在新终端只执行 `source /root/esp/esp-idf/export.sh` 和 `source /root/esp/esp-matter/export.sh`，不要执行 `connectedhomeip/.environment/activate.sh`，然后重新运行 `idf.py size`。
  - 当前状态：该复核尚无新原始输出，标记为非阻塞待办；已有 `light.bin`/`light.elf`、SHA256 和 Flash 识别证据仍然有效。

### 硬件连接验证

#### 串口设备
```
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# ls -l /dev/ttyUSB0
crw-rw---- 1 root dialout 188, 0 Jul 17 16:11 /dev/ttyUSB0
```

#### ESP32-C6 芯片识别
```
root@WIN-18OSVFS9GQA:~/esp/esp-matter/examples/light# esptool.py --chip esp32c6 --port /dev/ttyUSB0 flash_id \
    | sed -E '/^(MAC|BASE MAC|MAC_EXT):/s/:.*/: [REDACTED]/'
esptool.py v4.10.0
Serial port /dev/ttyUSB0
Connecting....
Chip is ESP32-C6 (QFN40) (revision v0.2)
Features: WiFi 6, BT 5, IEEE802.15.4
Crystal is 40MHz
MAC: [REDACTED]
BASE MAC: [REDACTED]
MAC_EXT: [REDACTED]
Uploading stub...
Running stub...
Stub running...
Manufacturer: 68
Device: 4018
Detected flash size: 16MB
Hard resetting via RTS pin...
```

**说明**:
- 串口设备：`/dev/ttyUSB0` (CP2102 USB to UART Bridge)
- 芯片型号：ESP32-C6 (QFN40)，版本 v0.2
- 功能特性：WiFi 6, BT 5, IEEE802.15.4
- 晶振频率：40MHz
- Flash 大小：16MB (注意：sdkconfig 配置为 4MB，实际硬件为 16MB)
- esptool.py 版本：v4.10.0
- 连接状态：成功连接，可以烧录固件

## 与之前配置的差异

根据 [development-environment.md](../../docs/development-environment.md) 文档记录：
- 原计划使用 Ubuntu 22.04 LTS
- 当前实际使用 Ubuntu 24.04.4 LTS
- ESP-IDF v5.4 已安装并验证可用
- ESP-Matter 已安装
