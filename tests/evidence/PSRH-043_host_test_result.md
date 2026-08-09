# PSRH-043 Integrated Host Test Result

- Firmware code merge: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD at test time:
  0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Date: 2026-08-09
- Command: make clean && make -j2 all
- Working directory: tests/host
- Compiler: GCC 15.2.0
- Compiler flags: -Wall -Wextra -Werror
- Exit code: 0
- Retained sanitized log:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout/integrated-c2a0ff0/host-test.log
- Log SHA-256:
  1b8f7d3e92e13ab77efe5064b47e010095d59ad7f18c131f624e00f9f32bd17f

All existing Host suites and the new health diagnostic suite passed:

| Suite | Passed |
|---|---:|
| occ_sm | 17 |
| ui_rgb | 20 |
| button_mode | 10 |
| env_alert_sm | 21 |
| night_window_sm | 25 |
| network_reconnect_sm | 34 |
| health_diag | 3 |
| **Total** | **130** |

The new tests cover normal/software/panic/watchdog/brownout/unknown reset
classification, stable class names, and the exact low-heap threshold
boundary. The final rerun completed with no compiler warning under -Werror.
This log is included in the top-level external evidence-files.sha256 manifest.
