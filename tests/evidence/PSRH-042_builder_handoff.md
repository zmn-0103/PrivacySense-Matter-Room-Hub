# PSRH-042 Builder Handoff

## Scope and commit binding

- Task contract: [`agent/tasks/PSRH-042.yml`](../../agent/tasks/PSRH-042.yml)
- Baseline commit: `02e67aa5216529ca83bff32bbf46ac1a8972e48d`
- Branch/worktree: `agent/psrh-042-matter-v15` / `PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15`
- Fixed independent review target: `2392a3b54564fcc270faa54d98bbdc7e3d923298`, `b9dee960936e61b136ffb19598abd21ae9f456f2`, `54014834049fb44e7f3564e40b334b438a3bd10c`, `587243575138a3983c6658c4eb991d89ffa1de2f`, `5d2e356312b998b3f22a3f6b4855b49632756e51`, `6f498dffdd38eeaea257b9e6f23597bb2b45731b`, reviewer-remediation implementation `2f5d58379360d759aa4c6ffd8c5574ff247c2cf5`, frozen-state metadata `560734bd254ac1953327149a787b5525c3229648`, historical HIL evidence `87a3f4157630ed639e3f7d99fe70079fc3e51684` (superseded), and authoritative production HIL evidence `0dda51d4aeef2a1717445dcb96c462094b8ed1ec`.
- Roles: builder lead `gpt-5.6-terra`; builder `gpt-5.6-luna` with reasoning effort `max`; independent reviewer `gpt-5.6-sol`.
- Independent review status: **PENDING RE-REVIEW**. The prior `REQUEST_CHANGES` conclusion remains in force until an independent reviewer accepts the remediation and the authoritative production-image controller success-path HIL; Builder does not self-certify a Reviewer PASS.
- Explicitly excluded from PSRH-042 functional delivery, but separately accepted by Human Lead on 2026-08-09: `c341e512600e673ca23fb73b377a4a8052d1b3a1` / `c341e51` (collaboration/governance documentation).
- Final implementation commit: `2f5d58379360d759aa4c6ffd8c5574ff247c2cf5` — addresses the four independent-review findings: controller request lifetime, atomic FORCE_SYNC generation, explicit HIL/release build gate, and retry-safe HIL exit state.
- New Fabric HIL follow-up commit: `5d2e356` — adds the opt-in five-minute NIGHT exit test variant and binds the sanitized HIL acceptance results recorded below.
- Controller success-path HIL evidence commit: `0dda51d4aeef2a1717445dcb96c462094b8ed1ec` — supersedes `87a3f415` and records the same persistent-storage loops on the `2f5d583` default production BIN/ELF, plus the paired serial transition/error scan.
- Final review handoff: the tip produced by this metadata closeout is the fixed final HEAD; after it, no firmware or handoff changes are to be made before independent review.
- Implementation paths in this closeout: `firmware/main/CMakeLists.txt`, `firmware/main/matter_app.cpp`, `firmware/main/matter_app.h`, `firmware/main/state_machine.c`, `firmware/main/state_machine.h`.
- Delivery metadata paths explicitly requested by Human Lead: `agent/tasks/PSRH-042.yml`, this handoff, `tests/evidence/PSRH-042_matter_delta_acceptance_20260808.md`, `tests/evidence/PSRH-042_controller_change_to_mode_hil_20260809.md`, and `docs/session-summary-20260808.md`.

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
| Frozen remediation production build | `ninja -C firmware/build -j2` — exit `0` | Tested image SHA-256: BIN `18571c257c0c4e459f4c92d6c7133fb6bd64abd155eec935749f0524f4f881ca`; ELF `74220b7982eca8707304e23e7cd298a950afc71e47778a0461f0295924924b67`; app image size `0x1d1120`. |
| Frozen remediation host tests | `make -C tests/host BUILD=/tmp/psrh-042-review-fix-host -j2 all` — `127/127 PASS` | This host suite does not replace the required physical controller success-path HIL. |
| Current HEAD production build | `HEAD=f8e5f9a`; environment loaded; `unset PSRH_HIL_NIGHT_EXIT_AFTER_MS; ninja -C firmware/build -j2` — exit `0` | Log SHA-256: `dd482e21dd055a5f8d7718c1aa108eb7850c50a6597f9a288651c1553606b574`; BIN `f44143e76e5d58507bcf9d363580a8846fa198d6f57e5eac96b02afb9c897bbf`; ELF `5115aff32c50875b29442a7193090e3790d02171339da889fff6240985c6e239`; app image size `0x1d1100` bytes. |
| Clean ESP32-C6 build | External fresh source/build from `b9dee96`; `ninja -C build -j2` — `1497/1497`, exit `0` | Persistent source/build root: `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/clean-build-b9dee96/`; build log hash: `53eb016ccf37229fbdf47ff8ebe9a1cc7122e540abbb2e50e68c332524ad2f46`. |
| Clean size | `idf.py -B build size` — exit `0` | Persistent size log hash: `a14be98d8b5757ba0da7a026597c5a0a5c9842e542edf538f68b9b915e6e2a90`. |
| Current image | BIN/ELF SHA-256 recorded below | Clean external-build images; outputs stay outside Git. |
| Formal baseline build | Independent temporary source tree from `02e67aa`, `ninja -C build -j2` — exit `1` | The formal baseline still uses the pre-ESP-Matter-1.5 `ModeOptionStruct` API and fails before linking; its log is retained outside the repository. No false baseline delta is claimed. |
| Pre-closeout comparison | Independent temporary source tree from `f6e9b6c`, `ninja -C build -j2` — `1497/1497`, exit `0` | Nearest compatible reference for the reviewer fix; persistent log hash: `afb5e58120b3d8c904243a14946d52808c5e7e7425f2699ea9fc198df8cb9547`. It is not substituted for the formal baseline. |
| Host/component tests | `127/127 PASS` (17+20+10+21+25+34) | Current rerun log hash: `5192e245d11802a0663c014caacb4ca8bb0a000639c60d91e8d16c39c9627b50`. The host suite does not cover Matter timeout, late-event cancellation, or request-slot reuse; test sources are outside this task's `owned_paths`, so adding such coverage requires a Reviewer/Human Lead decision. |
| Runtime stack evidence | Previous flashed image telemetry: radar 1980 B, UI 1156 B, state machine 4128 B, network 6256 B, env sensor 2192 B | No Matter-adapter high-water mark was captured; these values are not a post-patch HIL measurement. |
| HEAD restricted 5-minute HIL build | `HEAD=f8e5f9a`; `PSRH_HIL_NIGHT_EXIT_AFTER_MS=300000`; `ninja -C <external-hil-build-dir> -j2` — exit `0` | Log SHA-256: `38099f5a904335ae9f85ba14fe363f4d5633c2ec6c41d754954c10c18c847999`; BIN `04623b72727d6ad96eef8dc2d37407be1b8649ff661cdfbac2c684a6c9e3bee5`; ELF `5d1aba7100a33266e5f270bb02b7515586c7c34c5583ba4ee31e5fea1528c220`; CMake reported the 300000 ms override; production default remains unchanged. |
| HIL flash | `idf.py -B <external-hil-build-dir> -p /dev/ttyUSB0 flash` — PASS | Historical five-minute HIL variant; no `erase-flash`; NVS/Fabric remained. It is not the authoritative controller success-path image. |
| Frozen remediation production flash | `idf.py -B build -p /dev/ttyUSB0 flash` — PASS | `2f5d583` default production BIN `18571c257c0c4e459f4c92d6c7133fb6bd64abd155eec935749f0524f4f881ca`, ELF `74220b7982eca8707304e23e7cd298a950afc71e47778a0461f0295924924b67`; all four regions verified; no `erase-flash`, NVS/Fabric clear, or commissioning. |
| Controller HIL | **PASS for the ChangeToMode success path on the frozen remediation production image; other items remain bounded separately** | The authoritative controller loop and request-slot reuse are PASS; T14, T17, T19, and the device-restart endpoint pair remain PARTIAL at the evidence boundaries below. |

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
| Controller `ChangeToMode(1) → ChangeToMode(0)` success path | **PASS — frozen remediation production-image HIL** | Same persistent storage: `CurrentMode 0 → ChangeToMode(1) Success → 1 → ChangeToMode(0) Success → 0`; the loop was repeated once for request-slot reuse. Serial-side local commits matched all four transitions and the specified error scan was clear. Evidence: `tests/evidence/PSRH-042_controller_change_to_mode_hil_20260809.md` in authoritative commit `0dda51d4`; earlier `87a3f415` record is superseded. |
| NIGHT `ChangeToMode(2)` guard | **PASS — new Fabric HIL** | Controller request outside the window failed with sanitized `0x0000002F`. |
| T17 | **PARTIAL — new Fabric HIL** | Five-minute HIL variant captured automatic exit and post-exit `CurrentMode=0`; automatic entry was not separately captured. |
| T19 | **PARTIAL — new Fabric HIL** | Offline `NORMAL→QUIET` was captured; after AP restore `_matter._tcp` was not discovered and latest-value read timed out. |
| Controller restart with same storage | **PASS — new Fabric HIL** | Independent chip-tool processes reused persistent storage and re-established CASE. |
| Device restart CASE/EP reads | **PARTIAL — new Fabric HIL** | Ordinary restart restored CASE and EP2 read; post-restart EP1/EP2 pair was not both retained. |
| P1 independent review | **PENDING RE-REVIEW** | Historical conclusion is `REQUEST_CHANGES`; final status awaits independent review of `2f5d583`, the frozen production image, authoritative HIL evidence commit `0dda51d4`, and the final HEAD. |

## Evidence retention and safety

- The historical T14 log path was `/tmp/psrh-042-t14-mirrored-20260808-r2/commission.log`; the historical T15 log path was `/tmp/psrh-042-t15-mirrored-20260808/commission.log`. Both paths were checked and are absent. Therefore there is no source file to copy or SHA-256 to record; no replacement hash is fabricated.
- Persistent build, clean-build, size, host-test, and sanitized HIL evidence are stored outside the repository at `/home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-042-matter-v15/20260808/`; the retained build/size/test hashes are recorded above. The HIL evidence directory contains only sanitized conclusions and no raw controller/serial log.
- Raw controller logs, Fabric storage, credentials, setup payloads, MAC addresses, and complete IPv6 addresses are not committed.
- The authoritative 2026-08-09 production-image controller HIL raw chip-tool/serial captures remain outside the repository in the controlled `20260809/review-remediation-private/` artifact area; only the corrected sanitized summary in `0dda51d4` is authoritative. The earlier `87a3f415` summary is retained for audit history but superseded.
- A factory reset was explicitly authorized and performed before the new Fabric HIL. The 5-minute HIL image was explicitly authorized and flashed without `erase-flash`; no later NVS erase or re-commissioning was performed.
- Other branches were inspected read-only and not modified. Pre-existing collaboration/documentation commit `c341e51` changes `AGENTS.md`, `agent/task_templates/task-contract.yml`, and `docs/multi-agent-development.md`; it remains outside PSRH-042 functional delivery, but Human Lead separately accepted it on 2026-08-09, so it is no longer a merge blocker. No history rewrite was performed.

## Handoff condition

The controller `ChangeToMode(1) → ChangeToMode(0)` success-path HIL is now
recorded outside the NIGHT window on the frozen remediation production image
and bound to authoritative evidence commit `0dda51d4`.
The branch remains `PENDING_REVIEW` / historical `REQUEST_CHANGES`; it cannot
enter `READY_TO_MERGE` until an independent Reviewer reviews `2f5d583`, the corrected HIL
evidence, and the fixed final HEAD, then provides a final conclusion. Human Lead
must separately accept any remaining PARTIAL items before integration.
