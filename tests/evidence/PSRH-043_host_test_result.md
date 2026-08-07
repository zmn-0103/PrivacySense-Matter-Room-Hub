# PSRH-043 Host Test Result

- Commit under test: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Date: `2026-08-08`
- Command: `make clean && make -j2 all`
- Working directory: `tests/host`
- Compiler: GCC 15.2.0
- Compiler flags: `-Wall -Wextra -Werror`
- Exit code: `0`

All existing Host suites and the new health diagnostic suite passed:

| Suite | Passed |
|---|---:|
| `occ_sm` | 17 |
| `ui_rgb` | 20 |
| `button_mode` | 10 |
| `env_alert_sm` | 21 |
| `night_window_sm` | 25 |
| `network_reconnect_sm` | 34 |
| `health_diag` | 3 |
| **Total** | **130** |

The new tests cover normal/software/panic/watchdog/brownout/unknown reset
classification, stable class names, and the exact low-heap threshold boundary.
No compiler warning was emitted under `-Werror`.
