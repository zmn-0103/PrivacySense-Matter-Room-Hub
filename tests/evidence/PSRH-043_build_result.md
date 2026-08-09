# PSRH-043 Integrated ESP32-C6 Build Result

- Firmware code merge: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD used for this build:
  0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Integration baseline: 5044f9f854efdd4a9da899a357682c71605ec707
- PSRH-043 source tip: 433c5ec912716432b566879e20f37ec1af8ef078
- Date: 2026-08-09
- Target: esp32c6
- Toolchain: ESP-IDF 5.4.1
  (4c2820d377d1375e787bcef612f0c32c1427d183), ESP-Matter
  (a51f624f0735aefd0a9cfe1e0039d68de8ce24e2), Ninja 1.13.2, and
  riscv32 GCC 14.2.0 (esp-14.2.0_20241119)
- Fresh integrated build directory:
  /tmp/psrh-043-integrated-c2a0ff0.NTcPav
- Controlled artifact root:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout/integrated-c2a0ff0
- Top-level evidence manifest SHA-256:
  929d47f7d0e176b8611cddf3c31016e0440e2590de1a51bc40470d704036d143

## Integration handling

The source branch was integrated with a third independent worktree using
git merge --no-ff. Git reported no textual conflicts. The standalone
cherry-pick of 433c5ec was empty because its governance-file changes are
patch-equivalent to the already accepted c341e51 in the PSRH-042 baseline; the
merge endpoint still brought in the complete PSRH-043 history.

The merge result changed only PSRH-043 implementation/evidence paths.
firmware/main/matter_app.*, firmware/main/state_machine.*, approved
interfaces/data-model documents, and hardware paths remained byte-for-byte
equal to the integration baseline.

## Configure, build and size commands

Run from firmware with the exported environment:

~~~sh
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2

idf.py -B /tmp/psrh-043-integrated-c2a0ff0.NTcPav set-target esp32c6
idf.py -B /tmp/psrh-043-integrated-c2a0ff0.NTcPav reconfigure
ninja -C /tmp/psrh-043-integrated-c2a0ff0.NTcPav -j2 all
idf.py -B /tmp/psrh-043-integrated-c2a0ff0.NTcPav size
idf.py -B /tmp/psrh-043-integrated-c2a0ff0.NTcPav size-files --format csv
~~~

| Command | Exit code | Result |
|---|---:|---|
| idf.py set-target esp32c6 | 0 | Fresh target configuration created |
| idf.py reconfigure | 0 | Ninja graph generated; GN reported 5902 targets from 467 files |
| ninja -j2 all | 0 | 1498/1498 targets completed; application and bootloader linked |
| idf.py size | 0 | Application map/ELF size measurement completed |
| idf.py size-files --format csv | 0 | Per-object size record retained |

The sanitized integrated logs are retained outside the repository:

| Log | Relative path | SHA-256 |
|---|---|---|
| Environment | integrated-c2a0ff0/env.log | ba16c41f75f23a4af67d8555875935521180243a38c774e18075a93f132c3c4c |
| Configure/reconfigure | integrated-c2a0ff0/configure.log | e4dbb14c4fc376cbceaa6bfe2736ecdc08b01213f43737f00d627cbd91fa183d |
| Full build | integrated-c2a0ff0/build.log | ebe350c91427e667073ffaee4f4c396e27915860f45e7c7196297b935ede4ad9 |
| Size | integrated-c2a0ff0/size.log | 198e392157edb27a9f4d16f2fa1b3d32b92d87f3c810326edaf7dc81fa648b45 |
| Size files CSV | integrated-c2a0ff0/size-files.csv | e79f72f3d53f489cc8ad8ab2195267321ccb1215c12be56dbb5fd8c38d350c19 |

The compatible baseline was rebuilt with the same flow. Its full build,
configure, size, and size-files log hashes are recorded in the resource
measurement evidence and the top-level manifest; the baseline build produced
application BIN 0x1d1120.

## Image and memory result

| Measurement | Integrated result |
|---|---:|
| Bootloader binary | 0x5670 B; 0x2990 B free (32%) |
| Application BIN | 0x1d16e0 B / 1,906,400 B; 0x32e920 B free in the 5 MiB app partition (64%) |
| Flash Code | 1,753,510 B |
| DIRAM | 243,701 B / 53.90%; 208,411 B free |
| DIRAM .bss | 90,888 B |
| DIRAM .data | 18,077 B |
| LP SRAM | 24 B / 0.15%; 16,360 B free |
| Total image reported by idf.py size | 1,906,295 B |

The fresh 5044f9f baseline is 1,904,928 B BIN (0x1d1120), 1,752,036 B
Flash Code, 241,781 B DIRAM, 88,968 B .bss, 18,077 B .data, 24 B LP SRAM,
and 1,904,821 B total image. The corrected integrated deltas are:
BIN +1,472 B, Flash Code +1,474 B, DIRAM/.bss +1,920 B, .data +0 B,
LP SRAM +0 B, and total image +1,474 B.

The integrated app artifact identity is:

| Artifact | SHA-256 |
|---|---|
| privacy-sense-matter-room-hub.bin | 3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c |
| privacy-sense-matter-room-hub.elf | c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc |
| privacy-sense-matter-room-hub.map | 1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f |

Hardware Lab flashed this exact BIN hash. The sanitized flash transcript is
`integrated-c2a0ff0/hil-flash.log`, SHA-256
`8b84eef08f8ed1ae133eb8d0751e2e042dbb1fda270dd70c3a331f5b4bb47719`, and all
HIL logs are bound to the same top-level artifact manifest.

## Warning result

The integrated warning result is classified in
PSRH-043_warning_classification.md. The integrated log contains no
PSRH-043 project-owned compiler warning; the baseline-only
network.c command_is_link_event unused warning is absent after integration.
The project CMake compatibility warning and upstream ESP-Matter/ConnectedHomeIP
and Kconfig warnings remain visible and are not suppressed.

This is a successful static integrated build result. The runtime resource
snapshot gate is now evidenced as PASS; protocol, sensor-recovery, controlled
Wi-Fi-disconnect, and power-cycle HIL remain open. See
PSRH-043_hardware_deferral.md; the task remains INTEGRATION.
