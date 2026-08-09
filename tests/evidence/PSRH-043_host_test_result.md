# PSRH-043 Integrated Host Test Result

- Commit under test: `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- Date: `2026-08-09`
- Command: `make clean && make -j2 all`
- Working directory: `tests/host`
- Compiler: GCC `15.2.0`
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
classification, stable class names, and the exact low-heap threshold
boundary. The final integration rerun completed with no compiler warning under
`-Werror`.
