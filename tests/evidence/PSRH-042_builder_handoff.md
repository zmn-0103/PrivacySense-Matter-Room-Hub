# PSRH-042 Builder Handoff

## Scope and commit binding

- Task contract: [`agent/tasks/PSRH-042.yml`](../../agent/tasks/PSRH-042.yml)
- Baseline commit: `02e67aa5216529ca83bff32bbf46ac1a8972e48d`
- Branch/worktree: `agent/psrh-042-matter-v15` / `PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15`
- Fixed independent review target: implementation commits `2392a3b54564fcc270faa54d98bbdc7e3d923298` and `b9dee960936e61b136ffb19598abd21ae9f456f2`, plus metadata commits `54014834049fb44e7f3564e40b334b438a3bd10c` and `587243575138a3983c6658c4eb991d89ffa1de2f`.
- Roles: builder lead `gpt-5.6-terra`; builder `gpt-5.6-luna` with reasoning effort `max`; independent reviewer `gpt-5.6-sol`.
- Independent review status: **SKIPPED** per Human Lead instruction for this closeout; no `gpt-5.6-sol` sign-off is claimed.
- Explicitly excluded from PSRH-042 functional delivery: `c341e512600e673ca23fb73b377a4a8052d1b3a1` / `c341e51` (separately synchronized collaboration/governance documentation).
- Final implementation commit: `b9dee960936e61b136ffb19598abd21ae9f456f2` — contains the timeout-cancellation fix and task-contract scope. This metadata follow-up records the clean-build evidence and final acceptance state for that implementation.
- Implementation paths in this closeout: `firmware/main/matter_app.cpp`, `firmware/main/matter_app.h`, `firmware/main/state_machine.c`, `firmware/main/state_machine.h`.
- Delivery metadata paths explicitly requested by Human Lead: `agent/tasks/PSRH-042.yml`, this handoff, `tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md`, and `docs/session-summary-20260808.md`.

The implementation fixes the locked SDK's asynchronous boundary without changing
the approved EP0/EP1/EP2 topology, clusters, attributes, mode values, or
offline semantics. `SupportedModesManager` now waits for a bounded state-machine
decision before allowing the SDK's `CurrentMode::Set()`. The state machine
rejects queue/snapshot/policy/update failures, saves the actual mode on entry to
NIGHT, and reports the result through a private static response handle. Local
EP1/EP2 projections use `attribute::report()` and retain generation-based
FORCE_SYNC retry requests when a report fails.

## Machine verification

| Check | Exact command/result | Evidence or limitation |
|---|---|---|
| Diff hygiene | `git diff --check` — PASS | Re-run immediately before each commit. |
| Current worktree build | `ninja -C firmware/build -j2` — exit `0` | Incremental verification only; persistent log hash: `94c9937ae75443682a5b22a349f213636a41a5b096cb25192ab419ded257d8c7`. |
| Clean ESP32-C6 build | External fresh source/build from `b9dee96`; `ninja -C build -j2` — `1497/1497`, exit `0` | Persistent source/build root: `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/clean-build-b9dee96/`; build log hash: `53eb016ccf37229fbdf47ff8ebe9a1cc7122e540abbb2e50e68c332524ad2f46`. |
| Clean size | `idf.py -B build size` — exit `0` | Persistent size log hash: `a14be98d8b5757ba0da7a026597c5a0a5c9842e542edf538f68b9b915e6e2a90`. |
| Current image | BIN/ELF SHA-256 recorded below | Clean external-build images; outputs stay outside Git. |
| Formal baseline build | Independent temporary source tree from `02e67aa`, `ninja -C build -j2` — exit `1` | The formal baseline still uses the pre-ESP-Matter-1.5 `ModeOptionStruct` API and fails before linking; its log is retained outside the repository. No false baseline delta is claimed. |
| Pre-closeout comparison | Independent temporary source tree from `f6e9b6c`, `ninja -C build -j2` — `1497/1497`, exit `0` | Nearest compatible reference for the reviewer fix; persistent log hash: `afb5e58120b3d8c904243a14946d52808c5e7e7425f2699ea9fc198df8cb9547`. It is not substituted for the formal baseline. |
| Host/component tests | `127/127 PASS` (17+20+10+21+25+34) | Persistent current log hash: `33de1b8f1d7430d45c8c04d2826df912e58f0e8aa3cf2758a559cea457d783e6`. This is the only closeout Host count retained. The host suite does not cover Matter timeout, late-event cancellation, or request-slot reuse; test sources are outside this task's `owned_paths`, so adding such coverage requires a Reviewer/Human Lead decision. |
| Runtime stack evidence | Previous flashed image telemetry: radar 1980 B, UI 1156 B, state machine 4128 B, network 6256 B, env sensor 2192 B | No Matter-adapter high-water mark was captured; these values are not a post-patch HIL measurement. |
| Controller HIL | **DEFERRED** | At the current WSL check `/dev/ttyUSB0` is absent, and no existing chip-tool Fabric storage was found. No empty storage or new commissioning was used. |

### Image and resource records

| Metric | Formal baseline (`02e67aa`) | Compatible reference (`f6e9b6c`) | Current closeout | Delta vs reference |
|---|---:|---:|---:|---:|
| Flash Code | unavailable: baseline does not compile | 1,749,534 B | 1,752,010 B | +2,476 B |
| Application BIN | unavailable: baseline does not compile | 1,902,416 B (`0x1d0750`) | 1,904,896 B (`0x1d1100`) | +2,480 B |
| Total image | unavailable: baseline does not compile | 1,902,319 B | 1,904,795 B | +2,476 B |
| DIRAM | unavailable: baseline does not compile | 241,581 B (53.43%) | 241,781 B (53.48%) | +200 B |
| LP SRAM | unavailable: baseline does not compile | 24 B (0.15%) | 24 B (0.15%) | +0 B |

Current image hashes from the verification build:

```text
privacy-sense-matter-room-hub.bin  2a84dc3969e215d987bfcb938c898ed5eaf00f47976c97d652e5096e84d9fe10
privacy-sense-matter-room-hub.elf  e2010e556ae3afa3f17bb18e60f689a9eb1e8c1b653eeef1b4abaf3e279f7042
```

Compatible-reference image hashes:

```text
privacy-sense-matter-room-hub.bin  f5125d2d2d828eae7fb07f9212a836b12d5a9990306eea7af0e58f30117749e6
privacy-sense-matter-room-hub.elf  6af848f1afba9ffeec69cd4adccc782bba655e5ad633ab71e5891f546eac4698
```

The formal baseline cells remain intentionally unavailable because that source
does not compile on the locked SDK. Its persistent failure-log hash is
`1860d4aa2c2114705e131e95fa151a8ccf74720502ffb475878defb92ff4f78a`.

## Acceptance status

| Item | Final status | Evidence boundary |
|---|---|---|
| T14 commissioning | **PASS — historical summary only** | Previous session recorded BLE/PASE/NOC/CASE/operational discovery success. The original `/tmp` log is gone; no new hash is claimed. |
| T14 Endpoint reachability | **PASS — historical summary only** | Previous summary recorded EP1/EP2 reads; the original controller storage is unavailable. |
| T05 Matter sub-item | **PARTIAL** | No complete EP1 Occupancy sequence was captured before radar disconnect, during disconnect, and after recovery. |
| T09/T10/T12 | **PASS — prior evidence, not rerun** | Explicitly excluded from this closeout rerun. |
| T15 commissioning | **PASS — historical summary only** | Previous session recorded success; the original `/tmp` log is gone and T15 was not rerun. |
| T16 short test | **DEFERRED** | No usable Fabric storage for the required `0 → 1 → 0` controller reads. |
| NIGHT `ChangeToMode(2)` guard | **DEFERRED** | Code, host, and build evidence exist, but no controller failure response was captured. |
| T17 | **DEFERRED** | Automatic NIGHT entry/exit was intentionally not tested. |
| T19 | **PARTIAL — recovery read only; synchronization insufficiently verified** | No actual offline local-state change followed by a post-recovery latest-value read was captured. |
| P1 independent review | **SKIPPED** | Explicitly skipped by Human Lead instruction; this handoff contains no independent Reviewer sign-off. |

## Evidence retention and safety

- The historical T14 log path was `/tmp/psrh-042-t14-mirrored-20260808-r2/commission.log`; the historical T15 log path was `/tmp/psrh-042-t15-mirrored-20260808/commission.log`. Both paths were checked and are absent. Therefore there is no source file to copy or SHA-256 to record; no replacement hash is fabricated.
- Persistent build, clean-build, size, host-test, and historical serial artifacts are stored outside the repository at `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/`; the final build/size/test hashes are recorded above. A persistent serial-observation artifact there has SHA-256 `91d67b46637f067eed253146ba1b2c22254cb0fb31145ce0c53e0a8b7c555029`. It contains only heartbeat telemetry and is not T14/T15/T16/T05/T19 proof.
- Raw controller logs, Fabric storage, credentials, setup payloads, MAC addresses, and complete IPv6 addresses are not committed.
- No `idf.py flash`, `erase-flash`, NVS erase, factory reset, commissioning, or re-commissioning was performed in this closeout.
- Other branches were inspected read-only and not modified. Pre-existing collaboration/documentation commit `c341e51` changes `AGENTS.md`, `agent/task_templates/task-contract.yml`, and `docs/multi-agent-development.md`; it is outside PSRH-042 delivery scope. No history rewrite was performed. The full branch requires separate Human Lead acceptance of that governance commit or an authorized split before merge.

## Handoff condition

The branch is deliverable only after the final verification build, size delta,
`git diff --check`, tests, implementation commit hash, and clean-worktree result
are inserted here and confirmed by `git status --short`. The metadata-only
follow-up commit that records this handoff is reported separately with delivery.
