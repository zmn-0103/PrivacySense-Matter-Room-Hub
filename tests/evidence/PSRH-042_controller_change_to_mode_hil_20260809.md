# PSRH-042 Controller ChangeToMode success-path HIL

日期：2026-08-09 13:50 CST  
分支：`agent/psrh-042-matter-v15`  
固件代码冻结点：`2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`

## 范围与前置条件

- 使用既有持久 controller storage；本轮未创建新 storage、未 commissioning、未清除 Fabric/NVS。
- 使用测试开始前已在设备上的现有镜像；本轮未烧录、未复位、未执行 `erase-flash`。
- 现有 HIL 镜像记录为 `PSRH_HIL_NIGHT_EXIT_AFTER_MS=300000` 变体，BIN SHA-256：
  `04623b72727d6ad96eef8dc2d37407be1b8649ff661cdfbac2c684a6c9e3bee5`；
  ELF SHA-256：
  `5d1aba7100a33266e5f270bb02b7515586c7c34c5583ba4ee31e5fea1528c220`。
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

所有 chip-tool 命令退出码均为 `0`。两轮读取和命令均通过同一既有持久 storage 完成。

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

该测试项的 PASS 不等同于独立 Reviewer PASS。任务仍保持 `PENDING_REVIEW`，历史 Reviewer 结论仍为 `REQUEST_CHANGES`；本证据、`2f5d583` 和最终 HEAD 需交由独立 Reviewer 复审。

