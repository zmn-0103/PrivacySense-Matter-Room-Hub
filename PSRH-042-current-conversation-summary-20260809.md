# PSRH-042 当前对话总结

日期：2026-08-09

## 当前状态

- 分支：`agent/psrh-042-matter-v15`
- 最后一个固件代码提交：`2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`
- 交接状态提交：`d369403`（仅更新任务契约与 Builder handoff）
- 任务状态：`PENDING_REVIEW`
- 独立 Reviewer 的历史结论：`REQUEST_CHANGES`
- 当前不具备 `READY_TO_MERGE` 条件。

`d369403` 及本总结的后续元数据提交均未修改 `firmware/`；用于生产烧录和后续 HIL 的代码仍冻结于
`2f5d583`。

## 已提交的 Reviewer 修复

`2f5d583` 修复了四项独立审查发现：

- 在 Matter 请求入队前释放请求互斥锁，避免高优先级状态机把合法的控制器
  `ChangeToMode` 请求误判为取消。
- FORCE_SYNC generation 改为 GCC 原子加/读，避免并发读改写丢失重试。
- HIL 改为默认关闭的显式 CMake gate；release 构建启用 HIL 会失败，并校验
  HIL 延迟范围。
- HIL 定时退出仅在 `room_state_update()` 成功后提交 timer/hold 状态；失败会
  保持可重试。

## 构建和静态验证

所有构建均限制并行度为 `-j2`。

| 验证 | 结果 |
|---|---|
| Host 回归 | `make -C tests/host BUILD=/tmp/psrh-042-review-fix-host -j2 all`：**127/127 PASS** |
| 生产构建 | `ninja -C firmware/build -j2`：**PASS**，BIN 大小 `0x1d1120` |
| HIL/release 隔离 | HIL 与默认 release 同时配置时按预期 `FATAL_ERROR` |
| 独立 HIL 构建 | 显式 HIL 配置后 `ninja -C /tmp/psrh-042-hil-j2-final -j2`：**1321/1321 PASS**，BIN 大小 `0x1d12f0` |
| 差异卫生 | `git diff --check origin/main...HEAD`：**PASS** |
| 工作区 | **干净** |

生产固件 SHA-256：

```text
BIN  18571c257c0c4e459f4c92d6c7133fb6bd64abd155eec935749f0524f4f881ca
ELF  74220b7982eca8707304e23e7cd298a950afc71e47778a0461f0295924924b67
```

## 硬件执行边界

- 已确认串口设备并在 Human Lead 授权后烧录上述生产固件。
- 未执行 `erase-flash`、恢复出厂、NVS 清除或重新 commissioning。
- 使用既有持久 controller storage 的连通性读取能够建立 CASE；原始日志仅保留
  在仓库外的受控目录，未提交敏感字段。
- 当前处于 NIGHT 窗口，不能在此时证明生产固件的控制器
  `ChangeToMode(1) → ChangeToMode(0)` 正向路径。因此该项为 **DEFERRED**；
  物理按键 T16 与 NIGHT 保护命令均不是其替代证据。

## 交接更新

`d369403` 已更新：

- `agent/tasks/PSRH-042.yml`：将 `2f5d583` 与冻结元数据 `560734b` 纳入 review
  scope，并明确控制器正向成功路径为 DEFERRED。
- `tests/evidence/PSRH-042_builder_handoff.md`：将过时的 `SKIPPED` 改为
  `PENDING RE-REVIEW`，保留历史 `REQUEST_CHANGES`，并绑定当前生产构建、哈希和
  Reviewer 修复。

## 合并前剩余门禁

1. 在非 NIGHT 窗口以冻结生产固件、既有 Fabric 完成一次真实控制器
   `CurrentMode 0 → ChangeToMode(1) → 1 → ChangeToMode(0) → 0` HIL。
2. 仅提交脱敏的 HIL 摘要，并固定最终 HEAD。
3. 独立 Reviewer 复审 `2f5d583`、最终证据和最终 HEAD，给出 PASS 或新的
   `REQUEST_CHANGES`。
4. 即使 Reviewer PASS，仍由 Human Lead 接受剩余 PARTIAL 项并决定 Integration/
   `READY_TO_MERGE`；不自动推送、建 PR 或合并。

本轮未向仓库写入 setup payload、Wi-Fi 凭据、私钥、MAC、完整 IPv6、Fabric/Node
标识、controller storage 或原始控制器/串口日志。
