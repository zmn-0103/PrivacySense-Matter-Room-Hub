# PSRH-042 Builder Handoff

## Scope and commit binding

- Task contract: [`agent/tasks/PSRH-042.yml`](../../agent/tasks/PSRH-042.yml)
- Baseline commit: `02e67aa5216529ca83bff32bbf46ac1a8972e48d`
- Branch/worktree: `agent/psrh-042-matter-v15` / `PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15`
- Fixed independent review target: implementation commits `2392a3b54564fcc270faa54d98bbdc7e3d923298` and `b9dee960936e61b136ffb19598abd21ae9f456f2`, plus metadata commits `54014834049fb44e7f3564e40b334b438a3bd10c` and `587243575138a3983c6658c4eb991d89ffa1de2f`.
- Roles: builder lead `gpt-5.6-terra`; builder `gpt-5.6-luna` with reasoning effort `max`; independent reviewer `gpt-5.6-sol`.
- Independent review status: **SKIPPED** per Human Lead instruction for this closeout; no `gpt-5.6-sol` sign-off is claimed.
- Explicitly excluded from PSRH-042 functional delivery, but separately accepted by Human Lead on 2026-08-09: `c341e512600e673ca23fb73b377a4a8052d1b3a1` / `c341e51` (collaboration/governance documentation).
- Final implementation commit: `b9dee960936e61b136ffb19598abd21ae9f456f2` — contains the timeout-cancellation fix and task-contract scope. This metadata follow-up records the clean-build evidence and final acceptance state for that implementation.
- New Fabric HIL follow-up commit: `5d2e356` — adds the opt-in five-minute NIGHT exit test variant and binds the sanitized HIL acceptance results recorded below.
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

This handoff also includes a separate **new Fabric HIL** result set. It must
not be read as recovery evidence for yesterday's Fabric. The HIL used the
persistent storage path recorded in the acceptance summary; no credentials,
setup payload, private keys, MAC, complete IPv6 address, or raw storage was
committed.

## Machine verification

| Check | Exact command/result | Evidence or limitation |
|---|---|---|
| Diff hygiene | `git diff --check` — PASS | Re-run immediately before each commit. |
| Current worktree build | `ninja -C firmware/build -j2` — exit `0` | Latest restricted rerun log hash: `eb9358823c5af9346aeab7cb47014af704d8665f85f5decacd356f4090b36b26`; app image size `0x1d1100` bytes. |
| Clean ESP32-C6 build | External fresh source/build from `b9dee96`; `ninja -C build -j2` — `1497/1497`, exit `0` | Persistent source/build root: `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/clean-build-b9dee96/`; build log hash: `53eb016ccf37229fbdf47ff8ebe9a1cc7122e540abbb2e50e68c332524ad2f46`. |
| Clean size | `idf.py -B build size` — exit `0` | Persistent size log hash: `a14be98d8b5757ba0da7a026597c5a0a5c9842e542edf538f68b9b915e6e2a90`. |
| Current image | BIN/ELF SHA-256 recorded below | Clean external-build images; outputs stay outside Git. |
| Formal baseline build | Independent temporary source tree from `02e67aa`, `ninja -C build -j2` — exit `1` | The formal baseline still uses the pre-ESP-Matter-1.5 `ModeOptionStruct` API and fails before linking; its log is retained outside the repository. No false baseline delta is claimed. |
| Pre-closeout comparison | Independent temporary source tree from `f6e9b6c`, `ninja -C build -j2` — `1497/1497`, exit `0` | Nearest compatible reference for the reviewer fix; persistent log hash: `afb5e58120b3d8c904243a14946d52808c5e7e7425f2699ea9fc198df8cb9547`. It is not substituted for the formal baseline. |
| Host/component tests | `127/127 PASS` (17+20+10+21+25+34) | Current rerun log hash: `5192e245d11802a0663c014caacb4ca8bb0a000639c60d91e8d16c39c9627b50`. The host suite does not cover Matter timeout, late-event cancellation, or request-slot reuse; test sources are outside this task's `owned_paths`, so adding such coverage requires a Reviewer/Human Lead decision. |
| Runtime stack evidence | Previous flashed image telemetry: radar 1980 B, UI 1156 B, state machine 4128 B, network 6256 B, env sensor 2192 B | No Matter-adapter high-water mark was captured; these values are not a post-patch HIL measurement. |
| Restricted 5-minute HIL build | `PSRH_HIL_NIGHT_EXIT_AFTER_MS=300000`; `ninja -C <external-hil-build-dir> -j2` — PASS | Binary SHA-256: `04623b72727d6ad96eef8dc2d37407be1b8649ff661cdfbac2c684a6c9e3bee5`; production default remains unchanged. |
| HIL flash | `idf.py -B <external-hil-build-dir> -p /dev/ttyUSB0 flash` — PASS | No `erase-flash`; only bootloader/app/partition/OTA metadata were written; NVS/Fabric remained. Explicit Human Lead authorization was recorded in the session. |
| Controller HIL | **PARTIAL by test item** | T05, T16, and NIGHT protection PASS; T14, T17, T19, and the device-restart endpoint pair remain PARTIAL at the evidence boundaries below. |

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
| T14 commissioning/operational | **PARTIAL — new Fabric HIL** | Persistent storage, CASE, Operational Discovery, and EP1/EP2 reads were proven; raw PASE/NOC success chain was not retained. |
| T05 Matter sub-item | **PASS — new Fabric HIL** | EP1 Occupancy sequence `1 → 1 → 1` was captured across radar disconnect and recovery. |
| T09/T10/T12 | **PASS — prior evidence, not rerun** | Explicitly excluded from this closeout rerun. |
| T15 commissioning | **PASS — historical summary only** | Previous session recorded success; the original `/tmp` log is gone and T15 was not rerun. |
| T16 short test | **PASS — new Fabric HIL** | Same persistent storage read `CurrentMode 0 → 1 → 0`. |
| NIGHT `ChangeToMode(2)` guard | **PASS — new Fabric HIL** | Controller request outside the window failed with sanitized `0x0000002F`. |
| T17 | **PARTIAL — new Fabric HIL** | Five-minute HIL variant captured automatic exit and post-exit `CurrentMode=0`; automatic entry was not separately captured. |
| T19 | **PARTIAL — new Fabric HIL** | Offline `NORMAL→QUIET` was captured; after AP restore `_matter._tcp` was not discovered and latest-value read timed out. |
| Controller restart with same storage | **PASS — new Fabric HIL** | Independent chip-tool processes reused persistent storage and re-established CASE. |
| Device restart CASE/EP reads | **PARTIAL — new Fabric HIL** | Ordinary restart restored CASE and EP2 read; post-restart EP1/EP2 pair was not both retained. |
| P1 independent review | **SKIPPED** | Explicitly skipped by Human Lead instruction; this handoff contains no independent Reviewer sign-off. |

## Evidence retention and safety

- The historical T14 log path was `/tmp/psrh-042-t14-mirrored-20260808-r2/commission.log`; the historical T15 log path was `/tmp/psrh-042-t15-mirrored-20260808/commission.log`. Both paths were checked and are absent. Therefore there is no source file to copy or SHA-256 to record; no replacement hash is fabricated.
- Persistent build, clean-build, size, host-test, and sanitized HIL evidence are stored outside the repository at `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/`; the retained build/size/test hashes are recorded above. The HIL evidence directory contains only sanitized conclusions and no raw controller/serial log.
- Raw controller logs, Fabric storage, credentials, setup payloads, MAC addresses, and complete IPv6 addresses are not committed.
- A factory reset was explicitly authorized and performed before the new Fabric HIL. The 5-minute HIL image was explicitly authorized and flashed without `erase-flash`; no later NVS erase or re-commissioning was performed.
- Other branches were inspected read-only and not modified. Pre-existing collaboration/documentation commit `c341e51` changes `AGENTS.md`, `agent/task_templates/task-contract.yml`, and `docs/multi-agent-development.md`; it remains outside PSRH-042 functional delivery, but Human Lead separately accepted it on 2026-08-09, so it is no longer a merge blocker. No history rewrite was performed.

## Handoff condition

The branch is deliverable only after the final verification build, size delta,
`git diff --check`, tests, implementation commit hash, and clean-worktree result
are inserted here and confirmed by `git status --short`. The metadata-only
follow-up commit that records this handoff is reported separately with delivery.
