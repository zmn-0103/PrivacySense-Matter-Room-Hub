# PSRH-043 Builder Handoff

## Scope and baseline

- Task contract: `agent/tasks/PSRH-043.yml`
- Baseline commit: `02e67aa5216529ca83bff32bbf46ac1a8972e48d`
- Implementation commit: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Final commits: `045f37f87fce0b248a270767f6335b4b08f8724f`, `2fe9d38`
- Branch: `agent/psrh-043-phase5-reliability`
- Worktree: `PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-reliability`
- Date: `2026-08-08`

Changed owned paths:

- `firmware/CMakeLists.txt`
- `firmware/main/CMakeLists.txt`
- `firmware/main/health_diag.c`
- `firmware/main/health_diag.h`
- `firmware/main/main.c`
- `firmware/main/network.c`
- `tests/host/Makefile`
- `tests/host/test_health_diag.c`

Ownership check: all implementation changes are inside `owned_paths`. The
pre-created, untracked contract `agent/tasks/PSRH-043.yml` was read only and
was not modified or committed.

## Implementation summary

The new bounded diagnostic path reports current free heap, minimum free heap,
reset reason/class, aggregate minimum task stack high-water mark, and each
captured task's stack high-water mark. It uses a fixed 32-entry static
`TaskStatus_t` buffer and reports truncation instead of allocating more space.
The existing network task's 30-second diagnostic cadence is reused; no task,
queue, allocation, stack-size, watchdog, reset, safe-mode, OTA, partition,
GPIO, Matter data-model, or local-offline behavior was changed.

The pure reset and heap classifiers are Host-testable. Project-owned CMake
warning suppressions were removed; diagnostics are left visible and target
warnings are not declared clean without a target build.

## Machine verification

| Check | Exact command | Tool version | Result | Evidence |
|---|---|---|---|---|
| Diff hygiene | `git diff --check` | Git in workspace | PASS, exit 0 | This commit |
| Host tests | `make clean && make -j2 all` from `tests/host` | GCC 15.2.0 | PASS, exit 0; 130/130 tests | `PSRH-043_host_test_result.md` |
| ESP32-C6 build | `ninja -C /tmp/psrh-043-phase5-reliability-build -j2` | Ninja 1.13.2 | BLOCKED, exit 1; no `build.ninja` | `PSRH-043_build_result.md` |
| Hardware/HIL | No hardware command run | No board lease/toolchain | DEFERRED | `PSRH-043_hardware_deferral.md` |

## Resource and resilience evidence

- Flash/RAM delta: target build unavailable; explicit gap in
  `PSRH-043_resource_measurement.md`.
- Task stack high-water marks: collection/logging implemented, target capture
  not executed; explicit gap in `PSRH-043_resource_measurement.md`.
- Wi-Fi disconnect/recovery and sensor failure/recovery: unchanged and not
  re-tested on this isolated diagnostic commit.
- Matter/BLE behavior: deferred until the final PSRH-042-integrated firmware.

## Risks and decisions needed

- A target build with the approved ESP-IDF/ESP-Matter environment is still
  required for compile, warning, size, and runtime resource evidence.
- Hardware tests requiring integrated PSRH-042 firmware remain deferred.
- After PSRH-042 integration, rerun the final `ninja -C <isolated-build> -j2`
  build, size report, task/heap capture, and hardware acceptance before
  declaring Phase 5 PASS.
