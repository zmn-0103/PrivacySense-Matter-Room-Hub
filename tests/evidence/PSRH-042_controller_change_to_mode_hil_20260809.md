# PSRH-042 Controller ChangeToMode success-path HIL

日期：2026-08-09 14:14 CST
分支：`agent/psrh-042-matter-v15`
固件代码冻结点：`2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`
测试时分支 HEAD：`a03194f012b11d916a9607c1df44079638b89912`；该 HEAD 在 `2f5d583` 之后仅包含元数据变更。

## 范围与前置条件

- 使用既有持久 controller storage；本轮未创建新 storage、未 commissioning、未清除 Fabric/NVS。
- 使用 `2f5d583` 修复后的默认生产镜像；BIN SHA-256：
  `18571c257c0c4e459f4c92d6c7133fb6bd64abd155eec935749f0524f4f881ca`；
  ELF SHA-256：
  `74220b7982eca8707304e23e7cd298a950afc71e47778a0461f0295924924b67`。
- 测试前执行普通 `idf.py -B build -p /dev/ttyUSB0 flash`；bootloader、app、partition table、OTA data 均报告 `Hash of data verified`，随后硬复位。未执行 `erase-flash`，未清除 NVS/Fabric。
- 构建命令为 `ninja -C firmware/build -j2`；仅使用既有源码和构建产物，未修改 `firmware/`。
- 测试时处于 `22:00–07:00` NIGHT 窗口之外。
- 原始 chip-tool 与串口日志仅保留在仓库外受控目录；本文件不包含 setup payload、凭据、私钥、MAC、完整 IPv6、Fabric/Node 标识或原始日志。

## 控制器闭环结果

第一轮严格按要求执行：

```text
CurrentMode: 0
ChangeToMode(1): Success
CurrentMode: 1
ChangeToMode(0): Success
CurrentMode: 0
```

第二轮用于 request slot 复用：

```text
ChangeToMode(1): Success
CurrentMode: 1
ChangeToMode(0): Success
CurrentMode: 0
```

所有 chip-tool 命令退出码均为 `0`。两轮读取和命令均通过同一既有持久 storage 完成；本次测试绑定的是上述生产 BIN/ELF，不是此前 `f8e5f9a` 的五分钟 HIL 变体。

## 串口侧摘要

串口非复位采集确认了对应的本地转换：

```text
MATTER ChangeToMode: NORMAL → QUIET (controller)
ChangeToMode: controller transition committed, CurrentMode=1
MATTER ChangeToMode: QUIET → NORMAL (controller)
ChangeToMode: controller transition committed, CurrentMode=0
MATTER ChangeToMode: NORMAL → QUIET (controller)
ChangeToMode: controller transition committed, CurrentMode=1
MATTER ChangeToMode: QUIET → NORMAL (controller)
ChangeToMode: controller transition committed, CurrentMode=0
```

以下指定错误模式在本轮串口采集中均未出现：

- `queued request was cancelled`
- `response timed out`
- `commit cancelled/unavailable`
- `snapshot failed`
- `update failed`
- `response failed`
- `CurrentMode PRE_UPDATE mismatch`
- `local transition failed`

## 结论

**PASS — 控制器 `ChangeToMode(1) → ChangeToMode(0)` success path，包含一次 request slot 复用验证。**

该测试项的 PASS 不等同于独立 Reviewer PASS。此前提交 `87a3f415` 中绑定的 `f8e5f9a` HIL 镜像证据已被本次生产镜像实测 supersede；任务仍保持 `PENDING_REVIEW`，历史 Reviewer 结论仍为 `REQUEST_CHANGES`。本证据、`2f5d583` 和新的最终 HEAD 需交由独立 Reviewer 复审。
