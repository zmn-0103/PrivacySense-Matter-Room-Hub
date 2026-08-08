# PSRH-042 Builder Handoff

## Scope and baseline

- Task contract: [`agent/tasks/PSRH-042.yml`](../../agent/tasks/PSRH-042.yml)
- Baseline commit: `02e67aa5216529ca83bff32bbf46ac1a8972e48d`
- Branch/worktree: `agent/psrh-042-matter-v15` / `PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15`
- Final commit: recorded by `git rev-parse HEAD` after this handoff is committed and reported with delivery.
- Implementation paths: `firmware/main/matter_app.cpp`, `firmware/main/state_machine.c`
- Task/evidence paths: `agent/tasks/PSRH-042.yml`, `docs/session-summary-20260807.md`, `docs/session-summary-20260808.md`, `tests/evidence/PSRH-042_*.md`

The implementation remains within the approved PSRH-042 scope: ESP-Matter
1.5 SupportedModes API compatibility, the approved Occupancy Radar FeatureMap,
the Occupancy bitmap8 update, and synchronous plus state-machine NIGHT policy
guards. No new feature, public interface, endpoint, cluster, mode value,
partition, OTA path, or offline behavior was added in this closeout.

## Machine verification

| Check | Exact command/result | Evidence or limitation |
|---|---|---|
| Diff hygiene | `git diff --check` — PASS | Current worktree checks clean before commit. |
| ESP32-C6 build | `ninja -C firmware/build -j2` — exit `0` | Incremental verification; prior isolated clean build was `1497/1497` PASS. |
| Current build image | BIN SHA-256 `bfbf26c83c5deac054916d8458eb136a65d455b9c439218fa7f1116e604514e0`; ELF SHA-256 `dfa6b0d3373b145a965a0bccfa9de2cdeefa74451c3e9ed01176218976e64544` | Artifacts are local build outputs, not committed. |
| Host/component tests | Historical policy-fix result: `178/178 PASS` | See `PSRH-042_machine_verification_20260807.md`; not repeated in this metadata/evidence closeout. |
| Serial observation | `--no-reset` monitor, no reset/write; external log SHA-256 `91d67b46637f067eed253146ba1b2c22254cb0fb31145ce0c53e0a8b7c555029` | Only runtime heartbeats were captured; this is not T16/T05/T19 PASS evidence. |
| Controller HIL | **DEFERRED** | Host search found no existing `chip_tool_config*.ini` Fabric storage. No empty storage or new commissioning was used. |

The prior flashed policy-fix image hashes recorded in the historical evidence
were different from the current local build hashes, and the external build
directory is no longer available for byte-for-byte comparison. No reflash was
performed; exact image identity is therefore an explicit reproducibility gap,
not an unstated PASS.

## Acceptance status at handoff

| Item | Final status | Evidence boundary |
|---|---|---|
| T14 commissioning | **PASS — historical summary only** | Previous session recorded the complete BLE/PASE/NOC/CASE/operational discovery path. Raw `/tmp` log is gone; no SHA-256 can be recomputed. |
| T14 Endpoint reachability | **PASS — historical summary only** | Previous summary recorded EP1/EP2 reads; original controller storage is unavailable now. |
| T05 Matter sub-item | **PARTIAL** | Recovery read exists, but no Matter reads for before disconnect, during disconnect, and after recovery. |
| T09/T10/T12 | **PASS — prior evidence, not rerun** | Explicitly excluded from this closeout rerun. |
| T15 commissioning | **PASS — historical summary only** | Previous session recorded success; raw `/tmp` log is gone and T15 was not rerun. |
| T16 | **DEFERRED** | No existing Fabric storage; the required `0 → 1 → 0` short-press/read sequence was not captured. |
| NIGHT `ChangeToMode(2)` guard | **DEFERRED** | Code/Host/build evidence exists, but no real controller failure response was obtained. |
| T17 | **DEFERRED** | Automatic NIGHT entry/exit was intentionally not tested. |
| T19 | **PASS — recovery read only; synchronization insufficiently verified** | No actual offline local-state change followed by post-recovery latest-value read was captured. |

## Evidence retention and safety

- T14/T15 raw logs were previously under `/tmp` and are now absent. They were
  not copied to a persistent directory, and no replacement hash is claimed.
- Raw logs, controller Fabric storage, credentials, setup payloads, MACs, and
  full IPv6 addresses are not committed.
- No `idf.py flash`, `erase-flash`, NVS erase, factory reset, commissioning,
  or re-commissioning was performed in this closeout.
- Other branches were inspected read-only and not modified.

## Remaining decision

To complete T16, the NIGHT controller guard, T05, and T19 strictly, an existing
Matter controller Fabric storage and coordinated physical test actions are
required. Re-commissioning would create new evidence but is outside the
requested no-rerun boundary and must not be inferred from this handoff.
