# PSRH-043 Resource Measurement and Diagnostic Baseline

- Commit under review: `9d83e4b`
- Date: `2026-08-08`

## Implemented bounded baseline

- Current free heap and lifetime minimum free heap are captured through the
  heap capability API.
- Reset reason is captured and classified into normal, software, panic,
  watchdog, brownout, or unknown.
- Task stack high-water marks are captured through the existing FreeRTOS trace
  facility and logged per task in bytes, with an aggregate minimum. FreeRTOS
  reports the high-water value in `StackType_t` words, so the bounded capture
  converts it to bytes while the scheduler is suspended.
- The task snapshot has exactly 32 static records. The true task count,
  captured count, capacity, and truncation flag are logged. ESP-IDF's
  `uxTaskGetSystemState()` returns zero when the array is too small; this is
  exposed as `truncated=yes` rather than treated as a partial success.
- Task names are copied into fixed module-owned buffers while the scheduler is
  suspended; no `pcTaskName` pointer is used after task deletion can resume.
- The 16 KiB heap value is log-only. It does not reset, enter safe mode, alter
  watchdog behavior, or change allocation policy.
- No application task stack constant was increased.

## Measurement status

| Measurement | Status |
|---|---|
| Flash image size / delta | VERIFY_FAILED: application build stopped in read-only PSRH-042 `matter_app.cpp`; no application ELF/map |
| Bootloader size | Measured: `0x5670` bytes, `0x2990` bytes free (32%) |
| Static RAM delta | VERIFY_FAILED: application map unavailable; fixed snapshot storage is implemented but not measured in the final image |
| Runtime free/minimum heap | DEFERRED: requires a successfully built target firmware |
| Runtime task high-water marks | DEFERRED: requires a successfully built target firmware and startup logs |
| Power/temperature | DEFERRED: requires hardware |

These are intentional evidence gaps, not PASS claims. The target failure is a
real `VERIFY_FAILED`, not an external-environment `BLOCKED` result. They must
be filled on the final PSRH-042-integrated commit before Phase 5 is declared
PASS.
