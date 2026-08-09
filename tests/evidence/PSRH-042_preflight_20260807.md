# PSRH-042 preflight checkpoint

| 项目 | 结果 |
|---|---|
| 任务 | `PSRH-042` |
| 状态 | `SPEC`；未获人工批准 |
| 基线 | `02e67aa5216529ca83bff32bbf46ac1a8972e48d` |
| 主仓库 | `main` 与 `origin/main` 均指向上述基线；主 worktree 已恢复干净 |
| 隔离分支 | `agent/psrh-042-matter-v15` |
| 隔离 worktree | `/home/administrator/Project/PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15` |
| 修改范围 | `firmware/main/matter_app.cpp`，72 行新增、38 行删除 |
| 差异完整性 | `git diff --check` 通过；worktree 与保存 checkpoint 的补丁 SHA-256 均为 `09f2e8e408100c56191d82838c9e457160f0333eca2e35981d9523bbad9381d9` |
| 可恢复 checkpoint | Git stash `271f475f10b5360e4e8d88ce205f370fc9f2d075` |

## 当前内容

现有差异涉及 ESP-Matter 1.5 的 `ModeSelect` SupportedModes manager/API、属性回调枚举，以及 OccupancySensing Radar FeatureMap。它仍是隔离 worktree 中的未提交工作，不代表实现完成或验收通过。

## 验证缺口

- 目标构建：preflight 时未执行；任务现已批准，待 Builder 在独立构建目录中确认锁定的 ESP-IDF/ESP-Matter 实际 commit 后执行。
- Host/静态测试：未执行本任务专属验证。
- 实机烧录、Matter commissioning 和控制器互操作：未执行，明确延期。
- Flash、RAM、堆/栈和 warning 证据：尚无结果。

## Approval update

2026-08-07，Human Lead 通过本次任务指令批准 `PSRH-042` 进入 `APPROVED`。批准范围为：

- Builder `gpt-5.6-luna max` 继续使用 `agent/psrh-042-matter-v15` 独立分支和 worktree。
- 构建必须使用仓库外、任务专属的构建目录。
- EP0/EP1/EP2 拓扑、设备类型、Cluster、属性、模式值和离线语义保持冻结。
- Radar FeatureMap 是唯一明确批准的 Matter 数据模型增量；不得借此扩大其他接口或模型变更。
- 构建、size、测试、资源测量和交接证据仍是完成任务的必要条件。
