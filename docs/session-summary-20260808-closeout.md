# PSRH-042 当前会话收口摘要

日期：2026-08-08  
分支：`agent/psrh-042-matter-v15`  
工作树：`PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15`

## 1. 用户约束与范围

- `psrh-042-matter-v15` 为当前最高优先级分支；本轮不增加新功能，只做收口、证据整理和任务契约修正。
- 真实 HIL 必须使用已有 Fabric storage；不得新建 Fabric、重新 commissioning、擦除 NVS 或无授权烧录。
- T14、T09、T10、T12、T15 不重跑；T17 按要求继续 `DEFERRED`。
- 新日志不得依赖易因 WSL 重启丢失的 `/tmp`；重要证据放在仓库外持久目录，不提交原始日志或敏感信息。
- 用户最后明确要求跳过 P1 独立审查；因此本摘要和 handoff 均不声称 `gpt-5.6-sol` Reviewer sign-off。
- 构建沿用文档命令：仓库记录的实际命令为 `ninja -C firmware/build -j2`；外部 clean build 使用 `ninja -C build -j2`。

## 2. 代码与提交

基线：`02e67aa5216529ca83bff32bbf46ac1a8972e48d`

固定的 PSRH-042 功能/交付审查目标：

- 实现提交：`2392a3b54564fcc270faa54d98bbdc7e3d923298`、`b9dee960936e61b136ffb19598abd21ae9f456f2`。
- 元数据提交：`54014834049fb44e7f3564e40b334b438a3bd10c`、`587243575138a3983c6658c4eb991d89ffa1de2f`。
- `c341e512600e673ca23fb73b377a4a8052d1b3a1`（`c341e51`）是单独同步的协作/治理文档提交，不属于 PSRH-042 功能交付，未改写历史。
- 本次 P0 任务契约与证据修正提交：`608debbe303f34bbfd9a920e31fbf04a17894652`。

已完成的实现收口包括：

- `ChangeToMode` 在 SDK 写入 `CurrentMode` 前同步等待状态机决策。
- 队列、快照、策略、状态更新和超时失败转换为 Interaction Model 失败。
- NIGHT 在 SupportedModesManager 和状态机中双重校验。
- 超时请求取消、迟到事件拒绝、请求槽在迟到事件消费前不复用。
- 本地属性投影使用 `attribute::report()`，避免回调重新伪装为控制器命令。
- 属性报告失败设置代际 FORCE_SYNC 重试。
- QUIET → NIGHT 保存真实进入 NIGHT 前的模式。

任务角色已修正为：`builder_lead: gpt-5.6-terra`、`owner: gpt-5.6-luna`、`owner_reasoning_effort: max`、`reviewer: gpt-5.6-sol`。

## 3. 构建、测试与资源证据

- 外部 task-specific clean build 基于 `b9dee96`，命令 `ninja -C build -j2`，结果 `1497/1497`、退出码 `0`。
- 当前工作树增量构建命令 `ninja -C firmware/build -j2`，退出码 `0`。
- 持久 Host 测试唯一收口计数为 `127/127 PASS`（17+20+10+21+25+34）；不再保留 `129/129` 或其他冲突的收口计数。
- Host 套件不覆盖 Matter 请求超时、迟到事件取消和请求槽复用；测试代码不在本任务 `owned_paths` 中。P1 已按用户指令跳过。
- 当前 clean image：Flash Code `1,752,010 B`，BIN `1,904,896 B`，DIRAM `241,781 B`，LP SRAM `24 B`。
- 相对兼容参考 `f6e9b6c`：Flash Code `+2,476 B`，BIN `+2,480 B`，DIRAM `+200 B`，LP SRAM `+0 B`。正式基线因锁定 SDK 的旧 `ModeOptionStruct` API 不编译，不能给出数值增量。

持久证据根目录：

`/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/`

关键日志 SHA-256：

| 证据 | SHA-256 |
|---|---|
| 当前增量构建日志 | `94c9937ae75443682a5b22a349f213636a41a5b096cb25192ab419ded257d8c7` |
| Host `127/127` 日志 | `33de1b8f1d7430d45c8c04d2826df912e58f0e8aa3cf2758a559cea457d783e6` |
| 外部 clean build 日志 | `53eb016ccf37229fbdf47ff8ebe9a1cc7122e540abbb2e50e68c332524ad2f46` |
| clean size 日志 | `a14be98d8b5757ba0da7a026597c5a0a5c9842e542edf538f68b9b915e6e2a90` |
| 非破坏性串口观察日志 | `91d67b46637f067eed253146ba1b2c22254cb0fb31145ce0c53e0a8b7c555029` |

当前镜像哈希：

- BIN：`2a84dc3969e215d987bfcb938c898ed5eaf00f47976c97d652e5096e84d9fe10`
- ELF：`e2010e556ae3afa3f17bb18e60f689a9eb1e8c1b653eeef1b4abaf3e279f7042`

## 4. 验收状态

| 项目 | 当前状态 | 证据边界 |
|---|---|---|
| T14 commissioning | `PASS`，仅历史摘要 | 原始 `/tmp` 日志已不存在，未伪造新哈希；本轮不重跑。 |
| T14 Endpoint reachability | `PASS`，仅历史摘要 | 原控制器 storage 不可恢复；本轮不重跑。 |
| T05 Matter 子项 | `PARTIAL` | 缺少雷达断线前、断线期间、恢复后三个 EP1 Occupancy 读取点。 |
| T09/T10/T12 | `PASS`，既有证据，未重跑 | 不在本轮重跑范围。 |
| T15 | `PASS`，仅历史摘要 | 原始日志已不存在；本轮不重跑。 |
| T16 | `DEFERRED` | 未完成 CurrentMode `0 → 1 → 0` 控制器读取闭环。 |
| NIGHT 防护 | `DEFERRED` | 未取得真实 `ChangeToMode(2)` 失败响应。 |
| T17 | `DEFERRED` | 按要求不验证 NIGHT 自动进入/退出。 |
| T19 | `PARTIAL` | 仅有恢复后可读摘要，缺少断网期间改变本地状态并在恢复后读取最新值的闭环。 |
| P1 独立审查 | `SKIPPED` | 按用户最后指令跳过，无 Reviewer sign-off。 |

## 5. 真实 Fabric/HIL 状态

- 当前 WSL 检查时 `/dev/ttyUSB0` 不存在。
- 宿主只读搜索未找到既有 `chip_tool_config*.ini` 或可用 Fabric storage。
- 未创建空 storage、未 commissioning、未重新 commissioning、未执行 `idf.py flash`、擦除 NVS 或 factory reset。
- 若继续 P2，前置条件是用户重新挂载串口并提供已有 Fabric storage 路径；不得创建新的 Fabric。

## 6. 最终工作区

- 本次 P0 收口提交：`608debbe303f34bbfd9a920e31fbf04a17894652`；本摘要为其后的独立会话记录文件。
- `git diff --check`：通过。
- `git status --short --branch`：干净。
- 未提交原始 T14/T15 日志、Fabric storage、凭据、setup payload、MAC、完整 IPv6 地址或其他敏感信息。

相关正式文件：

- [`agent/tasks/PSRH-042.yml`](../agent/tasks/PSRH-042.yml)
- [`tests/evidence/PSRH-042_builder_handoff.md`](../tests/evidence/PSRH-042_builder_handoff.md)
- [`tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md`](../tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md)
- [`docs/session-summary-20260808.md`](session-summary-20260808.md)

会话期间曾发生上下文压缩；系统只能确认至少一次，未提供累计总次数。
