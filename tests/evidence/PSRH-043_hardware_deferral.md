# PSRH-043 Integrated Hardware Evidence / HIL Result

- Firmware code merge: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD at build time: 0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Date: 2026-08-09
- Status: PARTIAL; static and runtime-resource gates pass, but the task remains INTEGRATION
- Required integrated App BIN:
  3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c
- Controlled artifact root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout
- Controlled manifest: `evidence-files.sha256`
- Controlled manifest SHA-256:
  929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143

The Hardware Lab run used the authorized serial port and the exact App BIN
above. No NVS erase, factory reset, re-commissioning, credential injection,
or watchdog fault injection was performed. Raw serial/controller output was
not committed; the retained external logs are sanitized and hash-bound.

## Flash identity

The flash command wrote bootloader, partition table, OTA metadata, and the
application at `0x20000`. All four regions reported `Hash of data verified`;
the application write was 1,906,400 bytes and the flasher exited 0.

| Evidence | Relative path | SHA-256 |
|---|---|---|
| Flash transcript | integrated-c2a0ff0/hil-flash.log | 8b84eef08f8ed1ae133eb8d0751e2e042dbb1fda270dd70c3a331f5b4bb47719 |
| App BIN | integrated-c2a0ff0/privacy-sense-matter-room-hub.bin | 3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c |
| App ELF | integrated-c2a0ff0/privacy-sense-matter-room-hub.elf | c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc |
| App MAP | integrated-c2a0ff0/privacy-sense-matter-room-hub.map | 1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f |

## Runtime resource snapshots

The primary 100-second capture is `integrated-c2a0ff0/hil-runtime-02.log`,
SHA-256 `624ddcd33ad6440b8057bbaaaf17f3de64c946506132f88217368edc2ff19c7a`.
The independent ordinary-reset recovery capture is
`integrated-c2a0ff0/hil-runtime-recovery-01.log`, SHA-256
`6969a14b3cf8d7e572ff11f7ecf149a4f8ea900ff3544612301cb960e2d76cce`.
The first monitor attempt failed before opening the serial stream because it
had no TTY (`hil-runtime-01.log`, SHA-256
`dadbb36439b9ebdd770bfa7a33e1acd16745647970d5e2a9e4118069e87fbb1b`). It is
retained as a preflight record, not as runtime evidence. The primary monitor
wrapper was force-terminated by the bounded capture timeout after its 100
seconds; the firmware log contains the complete capture and no corresponding
firmware fault.

| Capture | tasks | captured | capacity | truncated | free heap | minimum free heap | minimum task HWM |
|---|---:|---:|---:|---|---:|---:|---:|
| boot | 4 | 4 | 32 | no | 272460 | 272460 | 1648 B |
| tasks_ready | 17 | 17 | 32 | no | 134912 | 119336 | 1156 B |
| network_periodic, about 32 s | 16 | 16 | 32 | no | 139660 | 119340 | 1276 B |
| network_periodic, about 62 s | 16 | 16 | 32 | no | 139660 | 119340 | 1276 B |
| network_periodic, about 92 s | 16 | 16 | 32 | no | 139468 | 119340 | 1276 B |
| recovery boot | 4 | 4 | 32 | no | 272460 | 272460 | 1648 B |
| recovery tasks_ready | 17 | 17 | 32 | no | 134808 | 119236 | 1276 B |
| recovery network_periodic | 16 | 16 | 32 | no | 139348 | 119256 | 1268 B |

The primary capture's complete task identities and HWM values are retained
in the log. The acceptance snapshots are also transcribed here:

~~~text
boot:
  main=2320, IDLE=1648, Tmr Svc=1728, esp_timer=3788

tasks_ready:
  main=1156, IDLE=1600, tiT=2988, sensor_radar=2128,
  sensor_env=2356, state_machine=4284, button=1384, network=6420,
  matter_adapt=10544, config=2344, CHIP=4884, Tmr Svc=1728,
  ui=1332, esp_timer=3644, console_repl=3292, sys_evt=3076,
  wifi=4152

network_periodic (~32 s):
  network=6364, config=2344, IDLE=1600, ui=1276,
  sensor_radar=2072, state_machine=4128, tiT=2604,
  sensor_env=2200, button=1384, CHIP=4884, Tmr Svc=1728,
  matter_adapt=10228, wifi=3724, esp_timer=3644,
  console_repl=3292, sys_evt=2920
~~~

Thus the hard resource snapshot conditions are PASS: every observed snapshot
has `tasks <= 32`, `captured == tasks`, and `truncated=no`; every captured
task has an identity and HWM; heap and minimum heap are recorded. The later
periodic snapshots also remained at 16/16 with capacity 32.

The periodic diagnostic window showed no TWDT failure, panic, Guru Meditation,
abort, stack overflow, or protocol timeout. The normal framework message
`Stopping the watchdog timer` is not a watchdog failure and no watchdog policy
was changed. Network diagnostics reported `ingress_overruns=0` and connected
state during the periodic captures.

## Protocol and recovery HIL

| Gate | Result | Evidence and boundary |
|---|---|---|
| Wi-Fi connect | PASS (observed) | Both serial runs show saved credentials, transient startup disconnects, then `STA got IP` and connected state. |
| Wi-Fi disconnect/recovery | PARTIAL | Boot-time disconnect/reconnect was observed, but no controlled post-connected AP disconnect was executed; the current lab has no approved AP control path. |
| Radar sensor | PASS (liveness only) | `ld2410c` heartbeat and HWM continued without driver crash. |
| DHT22 normal path | FAIL | Repeated `DHT22 parse fail`; no valid environmental sample was captured. |
| DHT22 failure path | PASS (observed) | The driver reported three consecutive failures and the UI entered `P1-sensor-fail`. |
| DHT22 recovery | NOT_CAPTURED | No valid sample/recovery occurred in the capture window; physical GPIO2, power, pull-up, and module wiring must be corrected or verified before retest. |
| Matter operational smoke | PASS | Using the same persistent controller storage, EP1 Occupancy read `1` and EP2 CurrentMode read `0`. ModeSelect `NORMAL -> QUIET -> NORMAL` read back `1 -> 0`. |
| Matter controller recovery | PASS | After the ordinary reset, the same storage completed CASE and read EP1 Occupancy `1` and EP2 CurrentMode `0`. |
| BLE commissioning/maintenance | PARTIAL | Exact-image logs show BLE host sync, commissioning-ready startup, already-commissioned advertisement disable, and successful BLE deinit. Re-commissioning was not repeated because no factory reset/setup payload was authorized. |
| Power-cycle recovery | NOT_CAPTURED | The monitor/RTS operation is a reset, not a verified removal and restoration of board power; no controlled power relay is present in this lab topology. |
| Watchdog observation | PASS for observation | No TWDT failure or policy change during the captured windows; deliberate watchdog fault injection remains forbidden. |

Matter/controller logs are retained and hash-bound:

| Evidence | SHA-256 |
|---|---|
| hil-matter-occupancy-02.log (sequential read, Occupancy=1) | 1800350bcbd8c9e2ea44f4958d1585a628e60a9917ddf75eb1f1a153f570168f |
| hil-matter-current-mode-quiet.log (CurrentMode=1) | 2342b1bdeac093a1a785b81c32a6a501e10c3a5d4211670c982a75a45997bd2b |
| hil-matter-current-mode-normal.log (CurrentMode=0) | fc23291059033059c84e90beda2d56f6b0e42e2f9bb3255a51191a88fb5053a3 |
| hil-matter-recovery-occupancy.log (Occupancy=1) | 01a8e0d3d548f841568b515d900183e27e9fcd34c25a30d6ec759a7300b0e03f |
| hil-matter-recovery-current-mode.log (CurrentMode=0) | cde7f19f7283929789d19fef22e27215d653e07ab2c6573c5f2b1dc2f0f68f96 |

The first Occupancy probe was launched concurrently with the first CurrentMode
probe and returned a controller-side `Resource is busy` error. It was rerun
sequentially and passed; this is retained as
`hil-matter-occupancy-01.log` (SHA-256
`7841d1db6f7d696f3e3f4a35ece75b4a77441452df889636c012c4ca5ee56cad`) and is
not counted as a firmware periodic-diagnostic timeout.

## Disposition

Runtime resource acceptance is now evidenced, but the overall HIL gate is not
closed. The task remains `INTEGRATION` until a lab run captures DHT22 normal
and recovery behavior, a controlled Wi-Fi disconnect/recovery, and a verified
power-cycle recovery (and, if required by the acceptance owner, a BLE
commissioning cycle). If any future diagnostic capture reports `truncated=yes`,
the static PASS is invalid for final acceptance and the capacity must be
corrected, rebuilt, and retested with a new exact BIN hash.
