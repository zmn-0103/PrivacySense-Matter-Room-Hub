# PSRH-043 Warning Classification

- Commit: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Date: `2026-08-08`

## Project-owned build flags

The project-owned `firmware/CMakeLists.txt` no longer adds:

- `-Wno-overloaded-virtual`
- `-Wno-format-nonliteral`
- `-Wno-format-security`

No new warning suppression was added. The initial target build was blocked
before compiler invocation, so target warnings cannot be classified as clean.
Any target warnings emitted after the approved toolchain is available must be
classified as project-owned or upstream/toolchain-originated without hiding
them.

## Verified result

The Host build uses `-Wall -Wextra -Werror` and completed with exit code `0`
and no warnings. This is Host evidence only; it does not substitute for the
ESP32-C6 warning summary.
