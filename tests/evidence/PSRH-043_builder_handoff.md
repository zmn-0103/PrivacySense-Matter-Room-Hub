# PSRH-043 Integrated Builder Handoff

## Identity and scope

- Task contract: agent/tasks/PSRH-043.yml
- Builder Lead: Human Lead
- Builder: Codex (Builder AI)
- Independent Reviewer: pending assignment; must be independent of Builder
- Review scope: PSRH-043 owned implementation, host tests, integrated
  ESP32-C6 build, warning classification, static resource delta, artifact
  identity, and the explicit runtime/HIL gate
- Integration baseline: 5044f9f854efdd4a9da899a357682c71605ec707
- PSRH-043 source tip: 433c5ec912716432b566879e20f37ec1af8ef078
- Firmware merge commit: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch: agent/psrh-043-phase5-integration
- Worktree:
  PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-integration
- Integration HEAD used for the fresh firmware build:
  0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Date: 2026-08-09
- Current disposition: INTEGRATION

The later evidence-only commits do not change firmware inputs. The
integration HEAD above is the exact source identity recorded by the fresh
integrated configure log; the firmware implementation is the c2a0ff0 merge.

## Metadata and delivery paths

The task contract explicitly declares the following delivery metadata:

- agent/tasks/PSRH-043.yml
- docs/session-summary-20260808-psrh-043.md
- tests/evidence/PSRH-043_*.md
- external artifact root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout
- external manifest:
  evidence-files.sha256, SHA-256
  929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143

The review includes the owned implementation paths only. The PSRH-042
Matter/state-machine files, approved interfaces/data-model documents, and
hardware paths remain read-only exclusions.

## Integration handling

The complete PSRH-043 history was merged into the independent integration
worktree with git merge --no-ff. There were no textual merge conflicts. The
standalone 433c5ec cherry-pick was empty because its governance patch is
equivalent to c341e51 already present in the baseline; skipping that empty
cherry-pick did not omit the endpoint history. The merge result changed only
PSRH-043 owned/evidence paths.

## Implementation summary

The bounded diagnostic path reports current free heap, minimum free heap,
reset class, aggregate minimum task stack high-water mark, and each captured
task high-water mark. It uses fixed 32-entry static storage, copies task names
while the scheduler is suspended, and reports the true task count plus
capture capacity/truncation. The existing network diagnostic cadence is
reused; no task stack, queue, unbounded allocation, watchdog, reset, safe
mode, OTA, partition, GPIO, Matter data-model, or local-offline behavior was
changed.

The pure reset and heap classifiers are Host-testable. The baseline
project-owned network.c unused-function warning was removed; project CMake
compatibility and upstream dependency warnings remain visible and classified.

## Machine verification

| Check | Result | Retained evidence |
|---|---|---|
| Merge hygiene | PASS; no textual conflicts; read-only paths unchanged | git history and task contract |
| Host tests | PASS; exit 0; 130/130 | PSRH-043_host_test_result.md; host-test.log SHA-256 1b8f7d3e92e13ab77efe5064b47e010095d59ad7f18c131f624e00f9f32bd17f |
| Fresh integrated configure | PASS; exit 0; GN 5902 targets / 467 files | configure.log SHA-256 e4dbb14c4fc376cbceaa6bfe2736ecdc08b01213f43737f00d627cbd91fa183d |
| Fresh integrated target build | PASS; exit 0; 1498/1498 | build.log SHA-256 ebe350c91427e667073ffaee4f4c396e27915860f45e7c7196297b935ede4ad9 |
| Integrated size | PASS; exit 0 | size.log SHA-256 198e392157edb27a9f4d16f2fa1b3d32b92d87f3c810326edaf7dc81fa648b45 |
| Warning classification | PASS as classification; no suppression | warning-classification.log SHA-256 496f164a30a7bf2cb029ba2c36db9c0f1753607b9aaa6f226a5ffaea15623d22 |
| Hardware/runtime HIL | PARTIAL; runtime resource gate PASS, protocol/sensor/power gates remain open | PSRH-043_hardware_deferral.md; external HIL logs |

The compatible baseline was rebuilt with the same fresh flow. Its App BIN is
0x1d1120, not the earlier unbound 0x1d0d80 value. Baseline build and size
logs, artifact hashes, and the integrated counterparts are all in the
external manifest.

## Resource and artifact identity

| Measurement | Baseline 5044f9f | Integrated | Delta |
|---|---:|---:|---:|
| Application BIN | 1,904,928 B (0x1d1120) | 1,906,400 B (0x1d16e0) | +1,472 B |
| Flash Code | 1,752,036 B | 1,753,510 B | +1,474 B |
| DIRAM/.bss | 241,781 B / 88,968 B | 243,701 B / 90,888 B | +1,920 B |
| .data | 18,077 B | 18,077 B | +0 B |
| LP SRAM | 24 B | 24 B | +0 B |
| Total image | 1,904,821 B | 1,906,295 B | +1,474 B |

Integrated App BIN:
3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c

Integrated App ELF:
c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc

Integrated App MAP:
1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f

Hardware Lab flashed the exact integrated BIN hash above and bound every
runtime/HIL log to the external evidence manifest. The flash transcript is
`integrated-c2a0ff0/hil-flash.log`, SHA-256
`8b84eef08f8ed1ae133eb8d0751e2e042dbb1fda270dd70c3a331f5b4bb47719`.

The primary 100-second serial log is
`integrated-c2a0ff0/hil-runtime-02.log`, SHA-256
`624ddcd33ad6440b8057bbaaaf17f3de64c946506132f88217368edc2ff19c7a`.
It captured `boot`, `tasks_ready`, and three `network_periodic` snapshots;
all were `truncated=no`, with `tasks/captured` of `4/4`, `17/17`, and
`16/16`. The ordinary-reset recovery serial log is
`integrated-c2a0ff0/hil-runtime-recovery-01.log`, SHA-256
`6969a14b3cf8d7e572ff11f7ecf149a4f8ea900ff3544612301cb960e2d76cce`.

Matter smoke and controller recovery passed using the same persistent storage:
EP1 Occupancy read `1`, EP2 CurrentMode read `0`, and the controlled
ModeSelect sequence read `1` for QUIET and `0` after restoration to NORMAL.
The serial logs show Wi-Fi startup disconnect/reconnect and Matter mDNS
publication, but no controlled post-connected Wi-Fi disconnect was available.
DHT22 produced repeated parse failures with no valid sample/recovery, and the
lab topology had no verified power-cycle control. BLE startup/synchronization
and post-commissioning deinit were observed; re-commissioning was not repeated
without a separately authorized factory-reset/setup-payload flow.

The task therefore remains `INTEGRATION`: static build/resource evidence and
the bounded runtime snapshot gate are PASS, while the remaining HIL gates and
independent Reviewer/Human Lead acceptance are pending. The complete list of
sanitized HIL log hashes is in `PSRH-043_hardware_deferral.md` and the
top-level external manifest.

## Handoff disposition

Static integrated build, Host tests, warning classification, corrected
resource measurement, and the bounded runtime snapshot gate are PASS for the
integration check. The task remains INTEGRATION: DHT22 normal/recovery,
controlled Wi-Fi disconnect/recovery, verified power-cycle recovery, any
required BLE commissioning cycle, independent Reviewer assessment, and Human
Lead acceptance are still required. If a future HIL snapshot reports
truncated=yes, the diagnostic capacity must be fixed and the image rebuilt
before acceptance. This handoff does not declare Phase 5 fully PASS, merge to
main, push, or create a PR.
