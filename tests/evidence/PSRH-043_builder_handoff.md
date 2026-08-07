# PSRH-043 Builder Handoff

## Scope and baseline

- Task contract: `agent/tasks/PSRH-043.yml`
- Baseline commit: `02e67aa5216529ca83bff32bbf46ac1a8972e48d`
- Implementation commit: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Review-fix/contract commit: `9d83e4b`
- Final commits: `045f37f87fce0b248a270767f6335b4b08f8724f`, `2fe9d38`, `d0b5851`, `9d83e4b`, `ef9d728`
- Branch: `agent/psrh-043-phase5-reliability`
- Worktree: `PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-reliability`
- Date: `2026-08-08`

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

Ownership check: implementation changes remain within the contract scope and
the read-only PSRH-042 Matter files were not modified. The pre-created task
contract is now committed in `9d83e4b`.

## Implementation summary

The new bounded diagnostic path reports current free heap, minimum free heap,
reset reason/class, aggregate minimum task stack high-water mark, and each
captured task's stack high-water mark. It uses fixed 32-entry static storage,
copies task names while the scheduler is suspended, and reports the true task
count plus capture capacity/truncation. If the task array is too small,
`uxTaskGetSystemState()` returns zero; the startup log now exposes that case.
The existing network task's 30-second diagnostic cadence is reused; no task,
queue, allocation, stack-size, watchdog, reset, safe-mode, OTA, partition,
GPIO, Matter data-model, or local-offline behavior was changed.

The pure reset and heap classifiers are Host-testable. Project-owned CMake
warning suppressions were removed; diagnostics are left visible. Because the
application did not link, a final target warning-free claim is not made.

## Machine verification

| Check | Exact command | Tool version | Result | Evidence |
|---|---|---|---|---|
| Diff hygiene | `git diff --check` | Git in workspace | PASS, exit 0 | This commit |
| Host tests | `make clean && make -j2 all` from `tests/host` | GCC 15.2.0 | PASS, exit 0; 130/130 tests | `PSRH-043_host_test_result.md` |
| ESP32-C6 configure | `idf.py ... set-target esp32c6` + `idf.py ... reconfigure` | ESP-IDF 5.4.1 | PASS, exit 0; Ninja graph generated | `PSRH-043_build_result.md` |
| ESP32-C6 build | `ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all` | Ninja 1.13.2 | VERIFY_FAILED, exit 1 in read-only PSRH-042 `matter_app.cpp` | `PSRH-043_build_result.md` |
| ESP32-C6 size | `idf.py -B /tmp/psrh-043-phase5-reliability-build size` | ESP-IDF 5.4.1 | VERIFY_FAILED, exit 2; bootloader only, no app map | `PSRH-043_build_result.md` |
| Hardware/HIL | No hardware command run | No board lease/toolchain | DEFERRED | `PSRH-043_hardware_deferral.md` |

## Resource and resilience evidence

- Flash/RAM delta: application compilation failed in the read-only PSRH-042
  Matter file; bootloader-only size is recorded in
  `PSRH-043_resource_measurement.md`.
- Task stack high-water marks: bounded collection/logging is implemented, but
  target runtime capture was not executed; the true task count is logged so a
  task set over 32 is visible.
- Wi-Fi disconnect/recovery and sensor failure/recovery: unchanged and not
  re-tested on this isolated diagnostic commit.
- Matter/BLE behavior: deferred until the final PSRH-042-integrated firmware.

## Risks and decisions needed

- The current target result is `VERIFY_FAILED`, not an environment block; the
  PSRH-042 owner must fix the existing Matter API mismatch in its own branch.
- A successful target build with the approved ESP-IDF/ESP-Matter environment
  is still required for application compile, warning, size, and runtime
  resource evidence.
- Hardware tests requiring integrated PSRH-042 firmware remain deferred.
- Do not merge this branch to `main` or declare Phase 5 PASS. After PSRH-042
  integration, rerun the exact `ninja -C <isolated-build> -j2 all` build,
  `idf.py ... size`, startup resource capture, and hardware acceptance.
