# 多 Agent 协作流程

## 1. 目的与适用范围

本流程使多个 AI 在可审计、可复现的前提下协作开发本项目。它不取代根目录 [AGENTS.md](../AGENTS.md)；后者的安全边界和角色限制优先。

默认开发链为：**人类 Lead → Terra 拆分任务/接口 → 人工批准 → Luna 或同等 Builder 实现 → Terra 汇总验证与集成准备 → Sol 独立审查 → 人工合并**。接口稳定且任务相互独立时可以并行 Builder；不因赶工取消独立审查。

## 2. 角色与责任

| 角色 | 责任 | 不得承担的工作 |
|---|---|---|
| 人类 Lead | 创建任务、批准接口和高风险变更、决定合并 | 把未批准的硬件假设当作事实 |
| Terra / Builder Lead | 拆分已批准任务、定义边界和验证、协调 Builder、汇总交接与集成准备 | 为自己或自己组织实现的提交给出独立审查结论 |
| Luna / Builder AI | 在分配的 worktree 和目录中实现、测试、提交与交接 | 改动未获批准的接口、越界修改或自我验收 |
| Sol / Reviewer | 独立审查代码、风险、测试方案、任务范围和交接证据 | 代替 Builder 编写业务代码，或直接操作硬件 |
| Integration | 从明确基线集成已经 Sol 审查的提交并执行完整检查；由 Terra 指派的独立 Builder 承担 | 解决未经批准的架构或语义冲突，或绕过重新审查 |
| Hardware Lab（后续） | 独占管理板卡、串口、调试器和测试设备 | 与其他任务共享未租约的硬件 |

## 3. 任务状态与人工门禁

```text
BACKLOG → SPEC → APPROVED → IMPLEMENTING → VERIFYING
        → INTEGRATION → HIL → READY_TO_MERGE → DONE
```

异常状态为 `BLOCKED`、`NEEDS_ARCHITECTURE_CHANGE`、`VERIFY_FAILED`、`INTEGRATION_FAILED`、`HIL_FAILED` 和 `REJECTED`。

以下变化必须先由人类批准：公共接口、Matter 数据模型、GPIO/连接表、时钟/启动/分区/内存布局、BLE/Wi-Fi 凭据处理、OTA/回滚、安全策略、功耗与执行器逻辑，以及任何降低测试或静态检查的做法。

## 4. 每个任务的执行方法

1. 从 [任务契约模板](../agent/task_templates/task-contract.yml) 创建 `agent/tasks/<task-id>.yml`，填写明确基线、目录所有权、约束、验收与证据。
2. Architecture 输出或更新 ADR、接口文档和测试计划；任务进入 `SPEC`。
3. 人类批准这些输入后，记录批准点，任务才可进入 `APPROVED`。
4. Terra 将每个子任务分配给 Builder；Builder 基于契约创建独立分支和 worktree，只在 `owned_paths` 范围内修改；需要改变冻结接口时停止并标记 `NEEDS_ARCHITECTURE_CHANGE`。
5. Builder 在本地运行允许的检查并提交，按[交接模板](../agent/handoff_templates/builder-handoff.md)提供机器证据；Terra 仅汇总这些事实和缺口，不将其改写为验收结论。
6. 对需合并的提交，Terra 指派未参与该提交实现的 Integration Builder，在独立 integration 分支上完成集成检查；发生冲突或语义变化时退回 Architecture/人类，而非现场自行决定。
7. Sol 在干净基线/提交上独立审查修改范围、并发与协议风险、验证缺口和证据；同一 Agent 不得既实现又审查该提交。问题退回对应 Builder 修复。
8. 人类确认验收、剩余风险和 Sol 审查结论后，才决定是否合并到受保护分支。

## 5. Worktree 与构建隔离

每个并行 Builder 必须有独立 worktree、分支和构建目录。例如（`<baseline-commit>` 必须写入任务契约）：

```powershell
git worktree add ..\PrivacySense-Matter-Room-Hub-worktrees\psrh-042-builder `
  -b agent/psrh-042-builder <baseline-commit>
```

ESP-IDF 构建目录同样必须隔离，不能让并行任务共用 `/root/build/privacy-sense`：

```bash
idf.py -B /root/build/privacy-sense/psrh-042-builder build
```

worktree、Agent 聊天记录、原始串口日志、板卡租约和临时构建产物均放在仓库外；不要提交。仓库只保存脱敏 Markdown 证据和原始制品的位置/哈希。

Integration 必须使用第三个独立分支和构建目录，按固定顺序引入已审查的提交；不得在 `main` 或任一 Builder worktree 内直接整合。Integration 的目标构建、尺寸测量和 HIL 结果属于集成交付，需要由 Sol 再次审查。

## 6. 资料、接口与证据

- `docs/approved_sources/`：登记已批准数据手册、勘误、原理图、BOM、PCB 版本和官方 SDK/RTOS 文档；记录版本、发布日期、适用硬件和文件哈希。
- `docs/interfaces/`：记录已批准的跨模块 API、事件、队列、错误语义和状态机边界。接口先审批、后并行实现。
- `docs/adr/`：记录会影响架构、接口、资源预算或安全边界的决策及取舍。
- `tests/evidence/`：仅保存脱敏的 Markdown 摘要；每份结果绑定 commit、固件哈希（如可得）、工具链、板卡/PCB、命令、退出码和原始日志位置。

## 7. 首个试点

首个任务应选接口稳定、影响范围小、可做 Host 测试的状态机或网络重连边界用例。不要将 Matter 实机 commissioning、分区/OTA、硬件连接或共享公共接口改动作为首个并行试点。

试点完成后再评估：首次验证通过率、接口返工次数、越界修改次数、测试可复现性及交付时间；只有在这些数据稳定后再提高并发数。
