# PSRH-043 Warning Classification

- Commit under review: `9d83e4b`
- Date: `2026-08-08`

## Project-owned build flags

The project-owned `firmware/CMakeLists.txt` no longer adds:

- `-Wno-overloaded-virtual`
- `-Wno-format-nonliteral`
- `-Wno-format-security`

No new warning suppression was added. The approved target toolchain was used;
the build reached application compilation but failed in the read-only
PSRH-042 Matter file before linking.

## Target warning summary

Warnings observed before the compile failure were classified as follows:

- Existing ESP-Matter/ConnectedHomeIP warnings: camera optional settings
  `maybe-uninitialized` diagnostics and Color Control `direction` possibly
  uninitialized. These are upstream dependency warnings; they remain visible
  and were not suppressed.
- Existing CMake compatibility/deprecation warnings from the project,
  ESP-Matter, ESP-IDF, and managed components, plus existing Kconfig choice
  and duplicate-symbol notices. These are configuration/dependency warnings,
  not evidence of a clean final target build.
- The project-owned unused `command_is_link_event()` warning in `network.c`
  was removed because it had no callers. No project-owned compiler warning was
  observed in the PSRH-043 sources after that cleanup.

Because the application did not link, this is not a final warning-free claim;
the integrated PSRH-042 build must repeat warning capture after its Matter
compile errors are fixed.

## Verified result

The Host build uses `-Wall -Wextra -Werror` and completed with exit code `0`
and no warnings. This is Host evidence only; it does not substitute for the
ESP32-C6 warning summary.
