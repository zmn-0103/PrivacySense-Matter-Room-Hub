# PSRH-043 Integrated Hardware Evidence / Deferral

- Firmware code merge: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD at build time:
  0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Date: 2026-08-09
- Status: DEFERRED; runtime/HIL is a hard gate
- Required integrated App BIN:
  3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c
- Controlled artifact manifest:
  evidence-files.sha256, SHA-256
  77b3bdb14380e6c508c803179a8329bb1ff40c8d533af4a0ce2060d44770e6be

The integrated configure, target build, Host tests, and static size checks
passed. No board, serial port, debugger, credentials, setup payload, MAC
address, or hardware lease was accessed in this integration worktree. No
runtime PASS is inferred from static build output.

## Required runtime snapshot

Hardware Lab must capture sanitized snapshots from the exact App BIN hash
above. The following fields are mandatory; they are all NOT_CAPTURED for this
integration run:

~~~text
boot=NOT_CAPTURED
tasks_ready=NOT_CAPTURED
network_periodic=NOT_CAPTURED
tasks=NOT_CAPTURED
captured=NOT_CAPTURED
capacity=32
truncated=NOT_CAPTURED
free_heap=NOT_CAPTURED
minimum_free_heap=NOT_CAPTURED
per_task_hwm=NOT_CAPTURED
twdt_during_periodic_diagnostics=NOT_CAPTURED
panic_during_periodic_diagnostics=NOT_CAPTURED
protocol_timeout_during_periodic_diagnostics=NOT_CAPTURED
~~~

The acceptance conditions are strict:

- tasks must be less than or equal to 32;
- captured must equal tasks;
- truncated must be no;
- every task must have a task identity and stack high-water mark;
- free heap, minimum free heap, and each task HWM must be recorded;
- the periodic diagnostic interval must complete without TWDT, panic, or
  protocol timeout.

If a real capture reports truncated=yes, this task does not pass. The fixed
snapshot capacity/diagnostic implementation must be corrected and the exact
integrated image must be rebuilt and retested before acceptance.

## Required protocol and recovery HIL

The authorized Hardware Lab run must also record sanitized PASS/FAIL evidence
for all of the following:

- Wi-Fi connect, disconnect, and recovery;
- sensor normal path, sensor failure, and sensor recovery;
- Matter commissioning/smoke and controller recovery;
- BLE commissioning/maintenance smoke where applicable;
- power-cycle recovery;
- watchdog observation with no unapproved watchdog-policy change.

During the run, the lab must bind serial/HIL logs to the integrated BIN SHA-256,
record the build manifest SHA-256, and retain the sanitized log hashes. Raw
serial logs, credentials, setup payloads, private keys, complete addresses,
and MAC addresses must not be committed.

Static Flash/RAM and bounded-storage measurements are recorded in
PSRH-043_resource_measurement.md. Runtime resource telemetry, protocol
recovery, and HIL behavior remain deferred pending explicit Hardware Lab
authorization and capture.
