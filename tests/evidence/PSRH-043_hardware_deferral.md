# PSRH-043 Integrated Hardware Evidence / HIL Result

- Firmware code merge: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD at build time: 0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Date: 2026-08-09
- Status: all requested HIL gates PASS; independently reviewed PASS at `4af6c635`; Human Lead accepted; task is READY_TO_MERGE
- Required integrated App BIN:
  3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c
- Controlled artifact root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout
- Controlled manifest: `evidence-files.sha256`
- Controlled manifest SHA-256:
  929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143
- Authorized BLE evidence root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/ble-direct-20260809
- BLE manifest SHA-256:
  4ef5aa8bd65f3f4dc0ff8f9cc5ccb6e3ccb4957a64c8e0198d631fc4e0b5b6fb
- Final HIL closeout evidence root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/hil-closeout-20260809
- Final HIL closeout manifest SHA-256:
  1792415f3734a81147d349d384999f0cf454c07ef6a75ea48d4f71e82e5362fb

The original integrated Hardware Lab run used the authorized serial port and
the exact App BIN above. That original run did not erase NVS, factory-reset,
or re-commission the device. A separately authorized follow-up on 2026-08-09
performed an exact `nvs`/`ps_cfg` erase and true BLE Wi-Fi commissioning with
the same unchanged App BIN; its evidence is recorded below. No firmware was
modified and no watchdog fault injection was performed. Raw
serial/controller output is retained outside the repository and hash-bound.

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
| Wi-Fi disconnect/recovery | PASS | After a successful operational CASE baseline, the connected AP was disabled. The device logged connected-to-disconnected, retained local sensor/radar/state-machine/UI behavior and healthy 16/16 snapshots, then regained IP, republished operational mDNS, completed new CASE sessions, and returned Occupancy `1` and CurrentMode `0`. |
| Radar sensor | PASS (liveness only) | `ld2410c` heartbeat and HWM continued without driver crash. |
| DHT22 normal path | PASS | After correcting GPIO2/power/ground/pull-up/module wiring, stable valid samples were captured, including 28.1-28.5 C and 70.5-71.9% RH before fault injection. |
| DHT22 failure path | PASS (observed) | The driver reported three consecutive failures and the UI entered `P1-sensor-fail`. |
| DHT22 recovery | PASS | Removing only DATA produced three consecutive parse failures, `online -> offline`, and `P1-sensor-fail`; restoring DATA produced a valid 27.9 C/74.0% RH sample, `offline -> online`, and repeated valid samples without reset. |
| Matter operational smoke | PASS | Using the same persistent controller storage, EP1 Occupancy read `1` and EP2 CurrentMode read `0`. ModeSelect `NORMAL -> QUIET -> NORMAL` read back `1 -> 0`. |
| Matter controller recovery | PASS | After the ordinary reset, the same storage completed CASE and read EP1 Occupancy `1` and EP2 CurrentMode `0`. |
| BLE commissioning/maintenance | PASS (authorized re-commissioning) | The unchanged exact image was run through an authorized precise `nvs`/`ps_cfg` erase, fresh BLE advertising was observed, and `ble-wifi` completed BLE GATT → PASE → NOC/CASE → Wi-Fi setup → operational CASE with `CommissioningComplete errorCode=0`. |
| Power-cycle recovery | PASS | Physical USB power removal made `/dev/ttyUSB0` disappear for 29 seconds (17:41:51 to 17:42:20). Re-enumeration was followed by operational mDNS, restored Fabric/CASE, Occupancy `1`, CurrentMode `0`, valid sensors, and 16/16 `truncated=no` snapshots. This was not RTS/reset. |
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

## Authorized BLE re-commissioning addendum (2026-08-09)

The user-authorized recovery procedure erased only the default credential NVS
partition (`0x9000`, `0x8000` bytes) and the business-config `ps_cfg`
partition (`0xA30000`, `0x4000` bytes). The App BIN, ELF, MAP, bootloader,
partition table, and OTA metadata were not changed or reflashed. Both erase
operations completed successfully on the ESP32-C6 and the device rebooted.

After the erase, the device published connectable Matter BLE advertising. The
new controller log records `New device scanned`, `Device discriminator match`,
BLE GATT connection, BTP negotiation, PASE `Pairing Success`, operational
certificate provisioning, `CASE establishment successful`, Wi-Fi network
setup, operational IPv6 CASE, `CommissioningComplete response errorCode=0`,
and `Device commissioning completed with success`. The initial rendezvous
and PASE path are explicitly BLE; the later operational discovery uses the
normal IPv6 Matter transport.

The new Fabric was then exercised sequentially: EP1 Occupancy read `1`, EP2
ModeSelect write/read `0 -> 1`, and write/read `1 -> 0`. After an ordinary RTS
reset, the device retrieved the new Fabric from NVS, found saved Wi-Fi
credentials, disabled BLE advertising for the commissioned state, reached
`STA got IP`, published operational `_matter._tcp`, and produced a healthy
`network_periodic` snapshot with `tasks=16`, `captured=16`, `capacity=32`,
`truncated=no`, and `ingress_overruns=0`. The subsequent property reads again
returned Occupancy `1` and CurrentMode `0`.

External evidence for this addendum:

| Evidence | SHA-256 |
|---|---|
| ble-direct-20260809/nvs-pscfg-erase-20260809.log | a281029e55ac5de0b6cf8cd155909c0d393720c2ed6ef7f6348f468e16575aba |
| ble-direct-20260809/commission-ble-wifi-20260809.log | 56bc518f1cab5edd615ec1a22b86b9a33866dca5bc584392b5794e309ef9b496 |
| ble-direct-20260809/serial-ordinary-reset-recovery-20260809.log | 189661f00bac80c19cf272fab636fbfc89f903c15ac4768c5e2f8edc19270dd1 |
| ble-direct-20260809/matter-ble-occupancy-20260809.log | 1e882098409296f3db15549b7d37f46522cb01da9d3a6c380366c3267bfbd2b7 |
| ble-direct-20260809/matter-ble-mode-1-20260809.log | 47271fbde193093e48947148f0b2506a2a8aefc7f4fbd389fd57f1341d03d974 |
| ble-direct-20260809/matter-ble-mode-read-1-20260809.log | 8baad4d152835947cfa605af6a8698488a5f1fed89ff7f4dafb95a4c023c54dc |
| ble-direct-20260809/matter-ble-mode-0-20260809.log | c683e4d2b171b75c05eb4833eb4aa89fe2fc559cd7c2d64fa7b9a148509e83b1 |
| ble-direct-20260809/matter-ble-mode-read-0-20260809.log | a234ee4fbd27bfd3667390278475fab48d1597eaa7f41fb2e3298f1fe7edf506 |
| ble-direct-20260809/evidence-files.sha256 | 4ef5aa8bd65f3f4dc0ff8f9cc5ccb6e3ccb4957a64c8e0198d631fc4e0b5b6fb |

## Final HIL closeout addendum (2026-08-09)

The final follow-up used the same unchanged App BIN and did not reflash or
modify firmware. The DHT22, controlled AP outage, and true power-cycle gates
were executed in that order. The controller identity used for acceptance was
commissioner `gamma` on the newly commissioned Fabric; earlier attempts with
the obsolete/default controller identity and one unsupported option were
retained in the 15-item manifest as operator-preflight records and are not
classified as firmware failures.

| Evidence | SHA-256 |
|---|---|
| hil-closeout-20260809/dht22-failure-recovery-20260809.log | da7655b730cbcd5231a82daf36798ea1c3448351972865481ccc137a0be71a63 |
| hil-closeout-20260809/wifi-baseline-gamma-occupancy-20260809.log | 0989708fcd0b7eff53bb117d1504fbf98b34bf9fe3bb705204c40af5f8f2fa7d |
| hil-closeout-20260809/wifi-baseline-gamma-current-mode-20260809.log | fcdaa3ce760cae976fdd0fca2968683a680f5c2002adf1db498cf551bf168390 |
| hil-closeout-20260809/wifi-disconnect-recovery-serial-20260809.log | 5a46b9a461a13985a747b41dff0f617e5d40953b36c2af0c4a168fecbd718ab9 |
| hil-closeout-20260809/wifi-recovery-gamma-occupancy-20260809.log | 6c937ec401d8cd9730dc1fe2dd089911c75d41c862a41266b51587acf5a859fe |
| hil-closeout-20260809/wifi-recovery-gamma-current-mode-20260809.log | 91b0fb65dcf8386225db7ebd640137f9f6c0720ff2fd59f3a8b5be18d2127ae4 |
| hil-closeout-20260809/power-cycle-serial-20260809.log | f274e64f8b3dfad56ce7ed3c1d95255a5af57d967da98d6e7e82d150ef4fa162 |
| hil-closeout-20260809/power-cycle-gamma-occupancy-20260809.log | a50a8ce1d5d107561e9702d1acbc71cf14f13dc539f76114ec41f50d6ec42e24 |
| hil-closeout-20260809/power-cycle-gamma-current-mode-20260809.log | 49528cb018501144b32e48c48de52557787f2d5d67003633c16226614dea556f |
| hil-closeout-20260809/evidence-files.sha256 (15 items) | 1792415f3734a81147d349d384999f0cf454c07ef6a75ea48d4f71e82e5362fb |

Across the closeout captures, every recorded resource snapshot remained at
16 tasks captured out of 16 with capacity 32 and `truncated=no`. The minimum
HWM was 1,180 B during the AP outage/recovery and 1,280 B after the true
power-cycle. No panic, abort, stack overflow, TWDT failure, protocol timeout,
or `truncated=yes` pattern was found.

## Disposition

Runtime resources and every requested HIL gate are evidenced PASS: BLE
re-commissioning, DHT22 normal/failure/recovery, controlled post-connected
Wi-Fi disconnect/recovery, Matter mDNS/CASE/attribute recovery, and true
power-cycle recovery. Independent Reviewer `gpt-5.6-sol` reviewed the fixed
HEAD and hash-bound evidence and concluded PASS. Human Lead explicitly
accepted the result on 2026-08-09, so the task is
`READY_TO_MERGE`. Any later firmware, image, manifest, or substantive evidence
change invalidates this closeout and requires fresh review.
