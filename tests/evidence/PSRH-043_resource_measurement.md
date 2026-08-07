# PSRH-043 Resource Measurement and Diagnostic Baseline

- Commit: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Date: `2026-08-08`

## Implemented bounded baseline

- Current free heap and lifetime minimum free heap are captured through the
  heap capability API.
- Reset reason is captured and classified into normal, software, panic,
  watchdog, brownout, or unknown.
- Task stack high-water marks are captured through the existing FreeRTOS trace
  facility and logged per task in bytes, with an aggregate minimum.
- The task snapshot has exactly 32 static records. A larger task set is
  reported as truncated; no buffer growth or runtime allocation occurs.
- The 16 KiB heap value is log-only. It does not reset, enter safe mode, alter
  watchdog behavior, or change allocation policy.
- No application task stack constant was increased.

## Measurement status

| Measurement | Status |
|---|---|
| Flash image size / delta | BLOCKED: ESP32-C6 target build unavailable |
| Static RAM delta | BLOCKED: target build/map unavailable; fixed snapshot storage is implemented but not measured |
| Runtime free/minimum heap | DEFERRED: requires target firmware |
| Runtime task high-water marks | DEFERRED: requires target firmware |
| Power/temperature | DEFERRED: requires hardware |

These gaps are intentional evidence gaps, not PASS claims. They must be
filled on the final PSRH-042-integrated commit before Phase 5 is declared PASS.
