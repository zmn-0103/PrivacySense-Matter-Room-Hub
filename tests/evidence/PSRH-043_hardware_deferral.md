# PSRH-043 Integrated Hardware Evidence / Deferral

- Commit under review: `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- Date: `2026-08-09`
- Status: **DEFERRED**

The integrated ESP32-C6 configure, build, and static size checks passed. No
board, serial port, debugger, credentials, setup payload, MAC address, or raw
serial log was accessed or committed in this worktree. No hardware lease was
available for this integration check.

The following runtime and hardware evidence remains deferred rather than
marked PASS:

- boot/reset-reason, current/minimum heap, and per-task stack high-water logs;
- confirmation of the runtime task count and 32-entry truncation behavior;
- Wi-Fi disconnect/recovery and sensor failure/recovery capture;
- Matter/BLE commissioning and controller recovery;
- power-cycle and watchdog-observation checks;
- final integrated-image HIL behavior and sanitized serial evidence.

The static Flash/RAM and bounded-storage measurements are recorded in
`PSRH-043_resource_measurement.md`. A separately authorized Hardware Lab run
must collect the runtime and protocol evidence before Phase 5 is declared
fully PASS.
