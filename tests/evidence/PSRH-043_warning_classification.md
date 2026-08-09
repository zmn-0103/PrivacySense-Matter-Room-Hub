# PSRH-043 Integrated Warning Classification

- Commit under review: `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- Date: `2026-08-09`
- Target command: `ninja -C /tmp/psrh-043-phase5-integration-build -j2 all`
- Result: exit `0`

## Project-owned build flags and warnings

The project-owned `firmware/CMakeLists.txt` no longer adds:

- `-Wno-overloaded-virtual`
- `-Wno-format-nonliteral`
- `-Wno-format-security`

No new warning suppression was added. The unused project function
`command_is_link_event()` was removed from `network.c` because it had no
callers. The integrated target build completed without a project-owned
compiler warning in the PSRH-043 sources.

## Warnings retained and classified

- Upstream ConnectedHomeIP/ESP-Matter camera optional-setting
  `maybe-uninitialized` diagnostics remain visible.
- The upstream Color Control `direction` `maybe-uninitialized` diagnostic
  remains visible.
- CMake compatibility/deprecation messages come from the project boilerplate
  and upstream ESP-IDF/ESP-Matter/managed components.
- Kconfig reports the existing duplicate `SEC_CERT_DAC_PROVIDER` choice
  definition/default diagnostics from ESP-Matter and ConnectedHomeIP.

These are upstream or dependency/configuration warnings, not reasons to hide
compiler diagnostics. The target build linked successfully; this is a
successful build with classified non-project warnings, not a claim that all
upstream warnings were eliminated.

The Host build separately used `-Wall -Wextra -Werror`, completed with exit
code `0`, and emitted no warning.
