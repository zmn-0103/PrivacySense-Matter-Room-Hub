# PSRH-042 Matter 增量验收摘要

日期：2026-08-08—2026-08-09
范围：本轮结果统一标记为**新 Fabric HIL**；不替代、覆盖或伪装为昨天既有 Fabric 的恢复证据。

## 新 Fabric HIL 结果

本轮使用持久 controller storage：
`/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/new-fabric-hil/controller-storage`。

closeout storage manifest snapshot SHA-256：`22711e42d0da1c19496e4c78205ffdf6d963c3c91b28120f448c409461d0220b`。

| 项目 | 结果 | 脱敏证据边界 |
|---|---|---|
| T14 BLE 配网与 Operational | **PARTIAL** | 新 Fabric storage 持久化；Operational Discovery、CASE、EP1/EP2 读取成功。PASE/NOC 原始成功链未保留，不能把整体标为 PASS。 |
| T05 Occupancy | **PASS** | 雷达断线前、断线中、恢复后三次 EP1 读取为 `1 → 1 → 1`。 |
| T16 短按模式切换 | **PASS** | 同一新 Fabric storage 读取 `CurrentMode 0 → 1 → 0`。 |
| NIGHT 防护 | **PASS** | 窗口外控制器 `ChangeToMode(2)` 被拒绝；脱敏错误码 `0x0000002F`。 |
| T17 NIGHT 自动转换 | **PARTIAL** | 5 分钟 HIL 变体直接捕获自动退出 `NIGHT → NORMAL`，退出后读取 `CurrentMode=0`；未单独捕获本轮变体的 `NORMAL → NIGHT` 自动进入。 |
| T19 离线本地状态同步 | **PARTIAL** | 离线时串口确认 `NORMAL→QUIET`；AP 恢复后本轮未重新发现 `_matter._tcp`，控制器最新值读取超时。 |
| 控制器重启恢复 | **PASS** | 多次独立 chip-tool 进程使用同一持久 storage 恢复 Fabric，并完成 CASE/读取。 |
| 设备重启后 CASE/读取 | **PARTIAL** | 普通重启（未擦 NVS、未重新 commissioning）后 CASE 与 EP2 读取成功；本次重启后的 EP1/EP2 双端点读取未同时保留。 |
| 重新 commissioning | **DEFERRED** | 本轮无必要，不重复清除 NVS/Fabric。 |

## 既有 Fabric 结果边界

T09/T10/T12/T15 的历史结果保持原记录，不作为本轮新 Fabric HIL 证据，也不与本轮结果合并计数。历史 T14/T15 原始 `/tmp` 日志已不可恢复，本摘要不伪造哈希。

## 明确未声称的结果

- T14 不声称原始 PASE/NOC 链可审计；仅声称本轮保留的 operational CASE/读取结果。
- T17 不把烧录后已有的 NIGHT 状态快照冒充自动进入证据。
- T19 不把 AP 恢复动作冒充 Matter Operational Discovery 或最新值读取；本轮恢复读取因 mDNS 未发现而失败。
- 普通设备重启不等同于断电重启；本轮未执行再次 commissioning。

## 证据与安全边界

- 新 Fabric HIL 脱敏证据位于仓库外持久目录 `.../new-fabric-hil/sanitized-evidence/`。
- 5 分钟 HIL 固件的编译定义为 `PSRH_HIL_NIGHT_EXIT_AFTER_MS=300000`；生产默认 NIGHT 窗口未改变。
- 未在提交、日志或摘要中保存 setup payload、Wi-Fi 凭据、私钥、MAC、完整 IPv6、Fabric/Node 标识或原始控制器 storage。
