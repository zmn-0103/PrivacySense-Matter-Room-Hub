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
  77b3bdb14380e6c508c803179a8329bb1ff40c8d533af4a0ce2060d44770e6be

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

The app artifacts are retained outside the repository and are the only images
eligible for the later HIL flash step.

| Image | Baseline SHA-256 | Integrated SHA-256 |
|---|---|---|
| privacy-sense-matter-room-hub.bin | cb90df1b0076eea370d021fe41f658b470a578355ce9366da2f338d7b3fcc9c0 | 3496c18164cb3410a3e19d83a6a1357151b4cea8ca20840f4492b58a8f81294c |
| privacy-sense-matter-room-hub.elf | a82ee1c77f94a368a9d8e41f20d93aeb2be0a9f467407167ad90617f9880bc8a | c56bd237359ec34685d2c795a72ea4a1be30c9477694648f69e3dddf198572cc |
| privacy-sense-matter-room-hub.map | 1bae9ba0a17690ffc53e01fd0e39458e21ddf0c03936e1c90ca7c43898b9febc | 1bf52bcdd61265757f8b4f839c32a2f877ed089ff466260acb2e69b64f5fab7f |

Bootloader, partition table, OTA data, and their hashes are in each
artifacts.sha256 file and the top-level evidence-files.sha256 manifest.
The integrated app BIN hash above must be recorded by Hardware Lab before
flashing and must match the image used for all runtime/HIL evidence.

## Runtime and hardware evidence

Static measurement is complete. Runtime resource and protocol evidence is not
captured in this integration worktree and remains a hard gate; see
PSRH-043_hardware_deferral.md. No runtime PASS is inferred from the static
ELF/map measurements.
