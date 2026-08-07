# PSRH-043 ESP32-C6 Build Result

- Commit: `045f37f87fce0b248a270767f6335b4b08f8724f`
- Date: `2026-08-08`
- Requested build form: `ninja -C <isolated-build-directory> -j2`
- Isolated build directory: `/tmp/psrh-043-phase5-reliability-build`

## Environment checks

| Exact command | Exit code | Result |
|---|---:|---|
| `command -v idf.py` | 1 | `idf.py` unavailable |
| `test -f /root/esp/esp-idf/export.sh && test -f /root/esp/esp-matter/export.sh` | 1 | ESP-IDF/ESP-Matter export scripts unavailable |
| `test -n "${IDF_PATH:-}" && test -n "${ESP_MATTER_PATH:-}"` | 1 | Required environment variables unset |
| `ninja --version` | 0 | `1.13.2` |

## Build attempt

Exact command:

```text
ninja -C /tmp/psrh-043-phase5-reliability-build -j2
```

Exit code: `1`

Sanitized output:

```text
ninja: Entering directory `/tmp/psrh-043-phase5-reliability-build'
ninja: error: loading 'build.ninja': No such file or directory
```

Status: **BLOCKED**, not PASS. The ESP32-C6 configure step could not be
performed because the approved ESP-IDF/ESP-Matter environment is absent, so
there is no generated Ninja graph.

ESP-IDF commit, ESP-Matter commit, Flash/RAM size, and target warning summary:
**not available**. No target warning-free claim is made. The final integrated
PSRH-042 commit must repeat configure/build with an isolated build directory,
`ninja -C <isolated-build-directory> -j2`, size inspection, and warning capture.
