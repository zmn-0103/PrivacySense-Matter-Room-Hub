# PSRH-043 Integrated Resource Measurement

- Firmware code merge under review: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD used for the fresh integrated build:
  0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- PSRH-043 source tip: 433c5ec912716432b566879e20f37ec1af8ef078
- Fresh compatible baseline: 5044f9f854efdd4a9da899a357682c71605ec707
- Date: 2026-08-09
- Target: esp32c6
- Toolchain: ESP-IDF 5.4.1
  (4c2820d377d1375e787bcef612f0c32c1427d183), ESP-Matter
  (a51f624f0735aefd0a9cfe1e0039d68de8ce24e2), Ninja 1.13.2, and
  riscv32 GCC 14.2.0 (esp-14.2.0_20241119)
- Controlled external artifact root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout
- Artifact manifest: evidence-files.sha256; SHA-256
  929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143

The baseline and integrated builds used the same fresh set-target +
reconfigure + ninja -j2 all + idf.py size flow. The temporary build
directories are retained only as build references; the flash artifacts and
sanitized logs were copied to the controlled external artifact root.

## Reproducible build records

Baseline source worktree:
PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15, commit
5044f9f854efdd4a9da899a357682c71605ec707.

Integrated source worktree:
PrivacySense-Matter-Room-Hub-worktrees/psrh-043-phase5-integration, HEAD
0d403f13ec0a0e4b2c32e16f35893f987606ae1d; firmware implementation is the
c2a0ff0 merge and the later HEAD change is evidence-only metadata.

Commands used for both builds:

~~~sh
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2
idf.py -B <fresh-build-dir> set-target esp32c6
idf.py -B <fresh-build-dir> reconfigure
ninja -C <fresh-build-dir> -j2 all
idf.py -B <fresh-build-dir> size
idf.py -B <fresh-build-dir> size-files --format csv
~~~

| Record | Fresh build directory | Configure | Build | Size |
|---|---|---:|---:|---:|
| Baseline 5044f9f | /tmp/psrh-043-baseline-5044f9f.XQ7l9N | 0 | 0; 1497/1497 | 0 |
| Integrated 0d403f1 / c2a0ff0 firmware | /tmp/psrh-043-integrated-c2a0ff0.NTcPav | 0 | 0; 1498/1498 | 0 |

The baseline size log is baseline-5044f9f/size.log with SHA-256
1526d31f5418d8d1ee0adb0e898b8fd0dbae8af097ec9b620ed6ebda7f54b2ec.
The integrated size log is integrated-c2a0ff0/size.log with SHA-256
198e392157edb27a9f4d16f2fa1b3d32b92d87f3c810326edaf7dc81fa648b45.
The corresponding sanitized full build logs are recorded in the manifest;
their SHA-256 values are
c985c4d4e47a9571ec6f83ab01d6a531c69e21b2a8eae05f31d66e52b7882e44 and
ebe350c91427e667073ffaee4f4c396e27915860f45e7c7196297b935ede4ad9.

## Static target measurement

The baseline values below come from the fresh 5044f9f build above, not from
the earlier unbound 0x1d0d80 artifact.

| Measurement | Baseline 5044f9f | Integrated | Delta |
|---|---:|---:|---:|
| Application BIN | 1,904,928 B (0x1d1120) | 1,906,400 B (0x1d16e0) | +1,472 B |
| App partition free | 0x32eee0 B (64%) | 0x32e920 B (64%) | -1,472 B |
| Flash Code | 1,752,036 B | 1,753,510 B | +1,474 B |
| DIRAM | 241,781 B | 243,701 B | +1,920 B |
| DIRAM .bss | 88,968 B | 90,888 B | +1,920 B |
| DIRAM .data | 18,077 B | 18,077 B | +0 B |
| LP SRAM | 24 B | 24 B | +0 B |
| Total image reported by size | 1,904,821 B | 1,906,295 B | +1,474 B |

The integrated size-files --format csv log attributes health_diag.c.obj
with 872 B of Flash Code and 1,920 B of DIRAM .bss. The ELF symbol table
gives the fixed storage directly:

| Symbol | Size |
|---|---:|
| s_task_records (32 x 24-byte records) | 0x300 / 768 B |
| s_task_status (32 x target TaskStatus_t) | 0x480 / 1,152 B |
| Fixed diagnostic storage total | 1,920 B |

No application task stack constant was increased. The existing constants
remain 6144 (state machine), 3072 (button), 3072 (UI), 8192 (network),
12288 (Matter adapter), and 4096 (config).

## Artifact identity

The app artifacts are retained outside the repository and were the only images
eligible for, and used by, the authorized HIL flash step.

| Image | Baseline SHA-256 | Integrated SHA-256 |
|---|---|---|
| privacy-sense-matter-room-hub.bin | cb90df1b0076eea370d021fe41f658b470a578355ce9366da2f338d7b3fcc9c0 | 3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c |
| privacy-sense-matter-room-hub.elf | a82ee1c77f94a368a9d8e41f20d93aeb2be0a9f467407167ad90617f9880bc8a | c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc |
| privacy-sense-matter-room-hub.map | 1bae9ba0a17690ffc53e01fd0e39458e21ddf0c03936e1c90ca7c43898b9febc | 1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f |

Bootloader, partition table, OTA data, and their hashes are in each
artifacts.sha256 file and the top-level evidence-files.sha256 manifest.
The integrated app BIN hash above was recorded by Hardware Lab before flashing
and matches the image used for all runtime/HIL evidence.

## Runtime and hardware evidence

The authorized Hardware Lab flash used the exact integrated App BIN above;
`integrated-c2a0ff0/hil-flash.log` records four `Hash of data verified` results,
application write size 1,906,400 bytes, and flasher exit 0. Its SHA-256 is
`8b84eef08f8ed1ae133eb8d0751e2e042dbb1fda270dd70c3a331f5b4bb47719`.

The primary serial capture (`hil-runtime-02.log`, SHA-256
`624ddcd33ad6440b8057bbaaaf17f3de64c946506132f88217368edc2ff19c7a`) and the
ordinary-reset recovery capture (`hil-runtime-recovery-01.log`, SHA-256
`6969a14b3cf8d7e572ff11f7ecf149a4f8ea900ff3544612301cb960e2d76cce`) report:

| Snapshot | tasks/captured | capacity | truncated | free heap | minimum free heap | minimum task HWM |
|---|---:|---:|---|---:|---:|---:|
| boot | 4/4 | 32 | no | 272460 | 272460 | 1648 B |
| tasks_ready | 17/17 | 32 | no | 134912 | 119336 | 1156 B |
| network_periodic (~32 s) | 16/16 | 32 | no | 139660 | 119340 | 1276 B |
| network_periodic (~62 s) | 16/16 | 32 | no | 139660 | 119340 | 1276 B |
| network_periodic (~92 s) | 16/16 | 32 | no | 139468 | 119340 | 1276 B |
| recovery boot | 4/4 | 32 | no | 272460 | 272460 | 1648 B |
| recovery tasks_ready | 17/17 | 32 | no | 134808 | 119236 | 1276 B |
| recovery network_periodic | 16/16 | 32 | no | 139348 | 119256 | 1268 B |

The complete HWM identities for the primary acceptance snapshots are retained
here so the resource gate is independently reviewable:

~~~text
boot: main=2320, IDLE=1648, Tmr Svc=1728, esp_timer=3788
tasks_ready: main=1156, IDLE=1600, tiT=2988, sensor_radar=2128,
sensor_env=2356, state_machine=4284, button=1384, network=6420,
matter_adapt=10544, config=2344, CHIP=4884, Tmr Svc=1728,
ui=1332, esp_timer=3644, console_repl=3292, sys_evt=3076,
wifi=4152
network_periodic (~32 s): network=6364, config=2344, IDLE=1600,
ui=1276, sensor_radar=2072, state_machine=4128, tiT=2604,
sensor_env=2200, button=1384, CHIP=4884, Tmr Svc=1728,
matter_adapt=10228, wifi=3724, esp_timer=3644,
console_repl=3292, sys_evt=2920
~~~

All observed snapshots satisfy `tasks <= 32`, `captured == tasks`, and
`truncated=no`. The periodic window had no TWDT failure, panic, abort, stack
overflow, or protocol timeout. The non-failure framework line that stops its
watchdog timer is retained in the serial log; watchdog policy was unchanged.

Protocol/HIL is not fully closed: Matter operational/controller recovery
passed, but DHT22 normal/recovery evidence and a controlled post-connected
Wi-Fi disconnect are incomplete, and no verified power-cycle was available.
See PSRH-043_hardware_deferral.md for the gate-by-gate disposition. Every
sanitized HIL log in the integrated artifact directory is included in the
top-level `evidence-files.sha256` manifest (42 entries; manifest SHA-256
`929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143`).
