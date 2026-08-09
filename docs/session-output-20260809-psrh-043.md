# PSRH-043 会话输出总结

## 结论

本次 PSRH-043 集成、构建、资源测量和授权 Hardware Lab 采集已完成。
静态构建与运行时资源快照通过，但传感器恢复、受控 Wi-Fi 断线和真正
power-cycle 尚未闭环，因此任务状态保持 `INTEGRATION`，不能标记为最终
验收 PASS。

## 集成身份

- 分支：`agent/psrh-043-phase5-integration`
- 集成基线：`5044f9f854efdd4a9da899a357682c71605ec707`
- PSRH-043 source tip：`433c5ec912716432b566879e20f37ec1af8ef078`
- Firmware merge：`c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- 构建输入 HEAD：`0d403f13ec0a0e4b2c32e16f35893f987606ae1d`
- 证据收口提交：`a21cf00bfd8c01162cb3e47977e0e5d67ff82a70`
- Builder Lead：Human Lead
- Builder：Codex (Builder AI)
- Independent Reviewer：待分配，必须独立于 Builder

## 构建与资源

- Fresh baseline App BIN：`0x1d1120`，1,904,928 B
- Integrated App BIN：`0x1d16e0`，1,906,400 B
- BIN 增量：`+1,472 B`
- Flash Code 增量：`+1,474 B`
- DIRAM/.bss 增量：`+1,920 B`
- Target build：1498/1498，exit 0
- Host tests：130/130，exit 0
- 项目自有 `network.c` unused warning 已消除；依赖和 CMake 兼容性 warning 已分类、未抑制

集成产物哈希：

| 文件 | SHA-256 |
|---|---|
| App BIN | `3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c` |
| App ELF | `c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc` |
| App MAP | `1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f` |

## Hardware Lab 结果

本轮烧录绑定上述 App BIN；bootloader、partition table、OTA metadata 和
application 均报告 `Hash of data verified`，应用写入 1,906,400 B。

| 快照 | tasks/captured | capacity | truncated | free heap | min-free heap |
|---|---:|---:|---|---:|---:|
| boot | 4/4 | 32 | no | 272460 | 272460 |
| tasks_ready | 17/17 | 32 | no | 134912 | 119336 |
| network_periodic | 16/16 | 32 | no | 139660 | 119340 |

后续周期快照仍为 16/16、`truncated=no`；所有任务身份和 HWM 已保存在
资源证据和脱敏串口日志中。周期诊断期间未发现 TWDT failure、panic、Guru
Meditation、abort、stack overflow 或 protocol timeout。

协议/恢复结果：

- Matter EP1 Occupancy 读取 `1`，EP2 CurrentMode 读取 `0`；ModeSelect
  `NORMAL → QUIET → NORMAL` 回读 `0 → 1 → 0`；普通复位后的控制器恢复读取通过。
- Wi-Fi 启动阶段观察到断开后重新获 IP并恢复连接；受控的已连接状态 AP 断开未执行。
- LD2410C radar heartbeat 正常；DHT22 连续解析失败，未捕获有效样本或传感器恢复。
- BLE 启动、host sync、已 commission 后停止 advertising 和 deinit 已观察；未重新 commissioning。
- 当前实验室没有受控电源继电器，power-cycle recovery 未捕获。

## 证据位置

- 详细 HIL：[PSRH-043_hardware_deferral.md](../tests/evidence/PSRH-043_hardware_deferral.md)
- 资源测量：[PSRH-043_resource_measurement.md](../tests/evidence/PSRH-043_resource_measurement.md)
- Builder handoff：[PSRH-043_builder_handoff.md](../tests/evidence/PSRH-043_builder_handoff.md)
- 构建结果：[PSRH-043_build_result.md](../tests/evidence/PSRH-043_build_result.md)
- 任务契约：[PSRH-043.yml](../agent/tasks/PSRH-043.yml)
- 外部受控产物根目录：
  `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout`
- 外部 `evidence-files.sha256`：42 项，SHA-256
  `929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143`

## 后续门禁

补齐 DHT22 接线/供电/上拉并重测正常与恢复路径；执行受控 Wi-Fi
disconnect/recovery；提供可验证的 power-cycle 控制并重测。若任一后续快照
出现 `truncated=yes`，必须修正诊断容量、重新构建并使用新的精确 BIN 重测。
