# PSRH-043 Sanitized Hardware Evidence / Deferral

- Commit: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Date: `2026-08-08`
- Status: **DEFERRED**

No board, serial port, debugger, credentials, setup payload, MAC address, or
raw serial log was accessed or committed in this worktree. The isolated
PSRH-043 branch is based on `02e67aa` and does not contain the final integrated
PSRH-042 firmware required for Matter/BLE and hardware validation.

The following evidence remains deferred rather than marked PASS:

- ESP32-C6 boot/reset-reason capture and heap/stack measurements;
- Wi-Fi disconnect/recovery and sensor failure/recovery capture;
- Matter/BLE commissioning and controller recovery;
- power-cycle and watchdog-observation checks;
- final flash/RAM/stack measurements on the integrated image.

After the final PSRH-042 Builder commit is integrated, rerun the isolated
ESP32-C6 build with `ninja -C <isolated-build-directory> -j2`, capture sanitized
resource logs, and record the hardware setup and exit/result status here.
