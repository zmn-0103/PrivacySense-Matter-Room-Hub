# 多 Agent 协作流程

## 1. 目的与适用范围

本流程使多个 AI 在可审计、可复现的前提下协作开发本项目。它不取代根目录 [AGENTS.md](../AGENTS.md)；后者的安全边界和角色限制优先。

首阶段只采用最小流程：**人类 Lead → 架构/接口说明 → 人工批准 → Builder AI 实现 → Codex 审查 → 人工合并**。在接口稳定、任务相互独立后，才增加并行 Builder、Integration 或 Hardware Lab 角色。

## 2. 角色与责任

| 角色 | 责任 | 不得承担的工作 |
|---|---|---|
| 人类 Lead | 创建任务、批准接口和高风险变更、决定合并 | 把未批准的硬件假设当作事实 |
| Architecture | 细化需求、状态机、接口、资源预算与测试计划 | 在无依据时假设电路或芯片行为 |
| Builder AI | 在分配的 worktree 和目录中实现、测试、提交与交接 | 改动未获批准的接口或越界修改 |
| Codex / Reviewer | 独立审查代码、风险、测试方案和交接证据 | 代替 Builder 编写业务代码，或直接操作硬件 |
| Integration（后续） | 从明确基线集成已验证提交并执行完整检查 | 解决架构或语义冲突 |
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
4. Builder 基于契约创建独立分支和 worktree，只在 `owned_paths` 范围内修改；需要改变冻结接口时停止并标记 `NEEDS_ARCHITECTURE_CHANGE`。
5. Builder 在本地运行允许的检查并提交，按[交接模板](../agent/handoff_templates/builder-handoff.md)提供机器证据。
6. Codex 在干净基线/提交上审查修改范围、并发与协议风险、验证缺口和证据；问题退回原 Builder 修复。
7. 人类确认验收、剩余风险和审查结论后，才合并到受保护分支。

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

## 6. 资料、接口与证据

- `docs/approved_sources/`：登记已批准数据手册、勘误、原理图、BOM、PCB 版本和官方 SDK/RTOS 文档；记录版本、发布日期、适用硬件和文件哈希。
- `docs/interfaces/`：记录已批准的跨模块 API、事件、队列、错误语义和状态机边界。接口先审批、后并行实现。
- `docs/adr/`：记录会影响架构、接口、资源预算或安全边界的决策及取舍。
- `tests/evidence/`：仅保存脱敏的 Markdown 摘要；每份结果绑定 commit、固件哈希（如可得）、工具链、板卡/PCB、命令、退出码和原始日志位置。

## 7. 首个试点

首个任务应选接口稳定、影响范围小、可做 Host 测试的状态机或网络重连边界用例。不要将 Matter 实机 commissioning、分区/OTA、硬件连接或共享公共接口改动作为首个并行试点。

试点完成后再评估：首次验证通过率、接口返工次数、越界修改次数、测试可复现性及交付时间；只有在这些数据稳定后再提高并发数。
