# PSRH-043 Integrated ESP32-C6 Build Result

- Commit under verification: `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- Integration baseline: `5044f9f854efdd4a9da899a357682c71605ec707`
- PSRH-043 source tip: `433c5ec912716432b566879e20f37ec1af8ef078`
- Date: `2026-08-09`
- Target: `esp32c6`
- Isolated build directory: `/tmp/psrh-043-phase5-integration-build`
- Ninja: `1.13.2`
- ESP-IDF: `5.4.1` (`4c2820d377d1375e787bcef612f0c32c1427d183`)
- ESP-Matter: `a51f624f0735aefd0a9cfe1e0039d68de8ce24e2`
- Cross compiler: GCC `14.2.0` (`esp-14.2.0_20241119`)

## Integration handling

The source branch was integrated with a third independent worktree using
`git merge --no-ff`. Git reported no textual conflicts. The standalone
cherry-pick of `433c5ec` was empty because its three governance-file changes
are patch-equivalent to the already accepted `c341e51` in the PSRH-042
baseline; the merge endpoint still brought in the complete PSRH-043 history.
The merge result changed only the PSRH-043 owned/evidence paths. The
PSRH-042 Matter files and other read-only paths remained byte-for-byte equal
to the integration baseline.

## Configure, build and size commands

Run from `firmware/` with the exported environment inherited by each command:

```sh
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export NINJAFLAGS=-j2

idf.py -B /tmp/psrh-043-phase5-integration-build set-target esp32c6
idf.py -B /tmp/psrh-043-phase5-integration-build reconfigure
ninja -C /tmp/psrh-043-phase5-integration-build -j2 all
idf.py -B /tmp/psrh-043-phase5-integration-build size
```

| Command | Exit code | Result |
|---|---:|---|
| `idf.py ... set-target esp32c6` | 0 | Fresh target configuration created |
| `idf.py ... reconfigure` | 0 | Ninja graph generated; GN reported 5902 targets from 467 files |
| `ninja ... -j2 all` | 0 | 1498/1498 targets completed; application and bootloader linked |
| `idf.py ... size` | 0 | Application map/ELF size measurement completed |

The build compiled both the PSRH-043 diagnostics and the frozen PSRH-042
Matter implementation. No application source or dependency compile error was
observed on the integrated commit.

## Image and memory result

| Measurement | Integrated result |
|---|---:|
| Bootloader binary | `0x5670` B; `0x2990` B free (32%) |
| Application BIN | `0x1d16e0` B / 1,906,400 B; `0x32e920` B free in the 5 MiB app partition (64%) |
| Flash Code | 1,753,510 B |
| DIRAM | 243,701 B / 53.90%; 208,411 B free |
| DIRAM `.bss` | 90,888 B |
| DIRAM `.data` | 18,077 B |
| LP SRAM | 24 B / 0.15%; 16,360 B free |
| Total image reported by `idf.py size` | 1,906,295 B |

The nearest compatible `5044f9f` PSRH-042 build evidence reports application
BIN `0x1d0d80`, Flash Code 1,751,112 B, DIRAM 241,781 B, `.bss` 88,968 B,
`.data` 18,077 B, LP SRAM 24 B, and total image 1,903,897 B. The integrated
delta is therefore: BIN `+2,400` B, Flash Code `+2,398` B, DIRAM/`.bss`
`+1,920` B, `.data` `+0` B, LP SRAM `+0` B, and reported total image
`+2,398` B.

## Warning classification

- No project-owned compiler warning was observed in the integrated PSRH-043
  sources; the unused `command_is_link_event()` warning was removed and no
  new warning suppression was added.
- Upstream ConnectedHomeIP/ESP-Matter warnings remain visible, including
  camera optional-setting `maybe-uninitialized` diagnostics and the Color
  Control `direction` `maybe-uninitialized` diagnostic.
- CMake compatibility/deprecation messages and the existing duplicate
  `SEC_CERT_DAC_PROVIDER` Kconfig choice messages are configuration/dependency
  warnings, not project-owned warning suppressions.
- The Host build separately completed with `-Wall -Wextra -Werror` and no
  warning.

This is a successful integrated build result. It is not a claim that upstream
dependency warnings were eliminated.
