# PSRH-043 ESP32-C6 Build Result

- Commit under verification: `9d83e4b`
- Date: `2026-08-08`
- Target: `esp32c6`
- Isolated build directory: `/tmp/psrh-043-phase5-reliability-build`
- Ninja: `1.13.2`
- ESP-IDF: `5.4.1`
- Cross compiler: GCC `14.2.0`

The earlier `BLOCKED` conclusion is withdrawn. The toolchain is available at
`/home/administrator/esp`; the earlier attempt used the wrong `/root/esp`
path and invoked Ninja before configuration had generated `build.ninja`.

## Configure and build commands

Run from `firmware/` in one shell so the exported environment is inherited by
all commands:

```sh
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2

idf.py -B /tmp/psrh-043-phase5-reliability-build set-target esp32c6
idf.py -B /tmp/psrh-043-phase5-reliability-build reconfigure
ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all
idf.py -B /tmp/psrh-043-phase5-reliability-build size
```

| Command | Exit code | Result |
|---|---:|---|
| `idf.py ... set-target esp32c6` | 0 | Fresh ESP32-C6 target configuration created |
| `idf.py ... reconfigure` | 0 | CMake/Ninja graph generated; GN reported 5902 targets from 467 files |
| `ninja -C /tmp/psrh-043-phase5-reliability-build -j2 all` | 1 | **VERIFY_FAILED** at `[1466/1498]` in `firmware/main/matter_app.cpp` |
| `idf.py ... size` | 2 | **VERIFY_FAILED**; retries `all` and cannot produce application map/size |

The target build compiled the GN graph and all PSRH-043 sources, including
`health_diag.c` and `network.c`, without a project-owned compiler warning. It
then failed in the pre-existing, read-only PSRH-042 Matter file
`firmware/main/matter_app.cpp` (Matter API/type errors including
`StaticSupportedModesManager`, callback designated initializers, and
`ModeOptionStruct`). This branch must not modify that file.

`idf.py size` did build the bootloader before reaching the same application
compile failure: bootloader size was `0x5670` bytes with `0x2990` bytes free
(32%). No application ELF/map was produced, so application Flash/RAM size is
not available.

Status: **VERIFY_FAILED**, not `BLOCKED` and not PASS. The PSRH-042 Builder
must resolve the Matter compile failure; after integration, repeat this exact
configure/build/size sequence and capture the final application size and
warning summary.
