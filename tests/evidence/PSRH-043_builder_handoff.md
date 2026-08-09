# PSRH-043 Integrated Builder Handoff

## Scope and integration

- Task contract: `agent/tasks/PSRH-043.yml`
- Integration baseline: `5044f9f854efdd4a9da899a357682c71605ec707`
- PSRH-043 source tip: `433c5ec912716432b566879e20f37ec1af8ef078`
- Integration merge commit: `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- Branch: `agent/psrh-043-phase5-integration`
- Worktree: `PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-integration`
- Date: `2026-08-09`

The complete PSRH-043 history was merged into the independent integration
worktree. There were no textual merge conflicts. The standalone `433c5ec`
cherry-pick was empty because its governance patch is equivalent to the
already present `c341e51`; this was handled by skipping the empty patch before
the endpoint merge. The merge result changed only PSRH-043 owned/evidence
paths. `firmware/main/matter_app.*`, `firmware/main/state_machine.*`, the
approved interfaces, data-model/architecture documents, and hardware paths
remain unchanged from the integration baseline.

Changed implementation/evidence paths:

- `firmware/CMakeLists.txt`
- `firmware/main/CMakeLists.txt`
- `firmware/main/health_diag.c`
- `firmware/main/health_diag.h`
- `firmware/main/main.c`
- `firmware/main/network.c`
- `tests/host/Makefile`
- `tests/host/test_health_diag.c`
- `tests/evidence/PSRH-043_*.md`
- `agent/tasks/PSRH-043.yml`

## Implementation summary

The bounded diagnostic path reports current free heap, minimum free heap,
reset class, aggregate minimum task stack high-water mark, and each captured
task's high-water mark. It uses fixed 32-entry static storage, copies task
names while the scheduler is suspended, and reports the true task count plus
capture capacity/truncation. The existing network task's 30-second diagnostic
cadence is reused; no task, queue, allocation, stack-size, watchdog, reset,
safe-mode, OTA, partition, GPIO, Matter data-model, or local-offline behavior
was changed.

The pure reset and heap classifiers are Host-testable. Project-owned CMake
warning suppressions were removed and diagnostics remain visible.

## Machine verification

| Check | Exact command | Tool/version | Result | Evidence |
|---|---|---|---|---|
| Merge hygiene | `git diff --check` | Git | PASS, exit 0 | Integration worktree |
| Host tests | `make clean && make -j2 all` from `tests/host` | GCC 15.2.0 | PASS, exit 0; 130/130 | `PSRH-043_host_test_result.md` |
| ESP32-C6 configure | `idf.py ... set-target esp32c6` + `idf.py ... reconfigure` | ESP-IDF 5.4.1 | PASS, exit 0; 5902 GN targets / 467 files | `PSRH-043_build_result.md` |
| ESP32-C6 build | `ninja -C /tmp/psrh-043-phase5-integration-build -j2 all` | Ninja 1.13.2 / GCC 14.2.0 | PASS, exit 0; 1498/1498 | `PSRH-043_build_result.md` |
| ESP32-C6 size | `idf.py -B /tmp/psrh-043-phase5-integration-build size` | ESP-IDF 5.4.1 | PASS, exit 0; app map/ELF measured | `PSRH-043_resource_measurement.md` |
| Hardware/HIL | No hardware command run | No board lease | DEFERRED | `PSRH-043_hardware_deferral.md` |

## Resource and resilience evidence

- Integrated application BIN: `0x1d16e0`; Flash Code: 1,753,510 B;
  DIRAM: 243,701 B; reported total image: 1,906,295 B.
- Delta against the compatible `5044f9f` build: BIN `+2,400` B, Flash Code
  `+2,398` B, DIRAM/`.bss` `+1,920` B; `.data` and LP SRAM unchanged.
- `health_diag.c.obj`: 872 B Flash Code and 1,920 B `.bss`.
- Fixed diagnostic storage: `s_task_records` 768 B plus `s_task_status`
  1,152 B, total 1,920 B.
- Main application task stack constants were not increased.
- Runtime heap and stack high-water logs, Wi-Fi/sensor recovery, Matter/BLE,
  power-cycle, and watchdog evidence remain deferred pending Hardware Lab
  authorization.

## Handoff disposition

The integrated target build and static resource measurement are PASS for the
integration check. The task remains in `INTEGRATION`: independent Reviewer
assessment, Human Lead acceptance, and the separately authorized runtime/HIL
evidence are still required. This handoff does not declare Phase 5 fully PASS,
does not merge to `main`, and does not push or create a PR.
