# PSRH-042 machine verification

| 项目 | 结果 |
|---|---|
| 任务状态 | `VERIFYING`; Builder bitmap8 fix and ChangeToMode NIGHT policy guard built/flashed; final Wi-Fi/Matter runtime passed; controller-command HIL, warning gate, and reviewer sign-off pending |
| Builder | `gpt-5.6-luna max` |
| Source baseline | `02e67aa5216529ca83bff32bbf46ac1a8972e48d` |
| Source diff | `firmware/main/matter_app.cpp`, `firmware/main/state_machine.c`; current uncommitted source diff SHA-256 `4cca9ae6c3ad89f0794e0b102d673cd8942d39a066f671106a9232c383f9d83f` |
| Worktree | `/home/administrator/Project/PrivacySense-Matter-Room-Hub-worktrees/psrh-042-matter-v15` |
| Build directory | `/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build` |
| Policy-fix build directory | `/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix` |
| Build parallelism | `-j2` through `NINJAFLAGS=-j2` |

## Actual toolchain

| Component | Actual path | Commit / version |
|---|---|---|
| ESP-IDF | `/home/administrator/esp/esp-idf` | `4c2820d377d1375e787bcef612f0c32c1427d183` (`v5.4.1`) |
| ESP-Matter | `/home/administrator/esp/esp-matter` | `a51f624f0735aefd0a9cfe1e0039d68de8ce24e2` |
| connectedhomeip | `/home/administrator/esp/esp-matter/connectedhomeip/connectedhomeip` | `cf84d0360c48dbc194c48b47b09169f302a9745b` |
| Python | ESP-IDF environment | `3.14.4` |
| CMake / Ninja | system tools | `4.2.3` / `1.13.2` |

## Commands and results

### Host tests

All commands used `-j2` where compilation was performed:

```text
make -C tests/host BUILD=/tmp/psrh-042-matter-v15-verification.KBATZe/tests-host -j2 all
make -C firmware/components/ld2410c/test clean
make -C firmware/components/ld2410c/test -j2 test_all
make -C firmware/components/ld2410c/test clean
make -C firmware/components/env_sensor/test clean
make -C firmware/components/env_sensor/test -j2 test
make -C firmware/components/env_sensor/test clean
```

Results:

- `tests/host`: 127/127 PASS (17 occupancy, 20 RGB, 10 button/mode, 21 environment alert, 25 night window, 34 network reconnect).
- LD2410C parser: 24/24 PASS.
- LD2410C transaction core: 16/16 PASS.
- DHT22 parser: 11/11 PASS.
- Total: **178/178 PASS**.
- Host compilation used `-Wall -Wextra -Werror` in all three Makefiles.

### ESP32-C6 clean build

```text
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1
export CCACHE_DIR=/tmp/psrh-042-matter-v15-verification.KBATZe/ccache
export NINJAFLAGS=-j2
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build fullclean
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build set-target esp32c6
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build reconfigure
NINJAFLAGS=-j2 idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build build
```

Result: **PASS**, Ninja completed `1497/1497` and generated the ESP32-C6 image. The first attempt was blocked before compilation because the default ccache directory was read-only; the successful clean rerun moved `CCACHE_DIR` into `/tmp`.

### `idf.py size`

```text
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build size
```

Summary:

| Resource | Used | Remaining / total |
|---|---:|---:|
| Flash Code | 1,749,000 B | — |
| Application binary | 1,901,888 B (`0x1d0540`) | 3,340,992 B / 5,242,880 B app partition (`0x32fac0`, 64% free) |
| DIRAM | 241,581 B (53.43%) | 210,531 B / 452,112 B |
| LP SRAM | 24 B (0.15%) | 16,360 B / 16,384 B |
| Bootloader | 22,128 B (`0x5670`) | 10,128 B (`0x2990`, 32% free) |

`idf.py size` also reported total image size `1,901,785` bytes (the binary is padded to `1,901,888` bytes).

## Warning classification

The build succeeded, but the warning gate is not clean:

### Project-owned warnings

- `firmware/CMakeLists.txt:19`: CMake deprecation warning for `cmake_minimum_required(VERSION 3.5)`.
- `firmware/main/network.c:199`: `command_is_link_event` is defined but unused (`-Wunused-function`).

Both files are outside this task's `owned_paths` and are unchanged by the current `matter_app.cpp` diff. They must be resolved or explicitly accepted before a final submission; they were not silently suppressed.

### Upstream/toolchain warnings

- ESP-Matter/connectedhomeip camera optional values and ColorControl `direction` may be used uninitialized.
- ESP-Matter/connectedhomeip Kconfig duplicate/out-of-choice definition for `SEC_CERT_DAC_PROVIDER`.
- CMake deprecation warnings from ESP-Matter, ESP-IDF mbedTLS, and the managed `esp-serial-flasher` component.

No warning was emitted from `matter_app.cpp` in this build.

## External artifacts

Raw logs and binaries remain outside the repository:

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-clean-build.log
/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-size.log
/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build.log  # initial ccache-blocked attempt
/tmp/psrh-042-matter-v15-verification.KBATZe/host-builder-fix.log
/tmp/psrh-042-matter-v15-verification.KBATZe/ld2410c-builder-fix.log
/tmp/psrh-042-matter-v15-verification.KBATZe/env-builder-fix.log
/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build/privacy-sense-matter-room-hub.bin
/tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build/privacy-sense-matter-room-hub.elf
```

SHA-256:

```text
firmware-clean-build.log  8ba92af4d8ab3592a8c8ffeb6310b0dc846a3a2f4f177f5d64d33f997ffe2dc1
firmware-size.log         8fa1efec1f3c9f86b6f8a57263f86bd025552af152e81e486b643d9887131243
firmware-build.log        ceb30f5853f995df2ce375c12c5b810c9fd5704a7ca039267a5d1fce95bdd0b6
host-builder-fix.log      5c311ab6eaa49596b8788d3bb9bcfad14081ac3ae64b672c41c595014660fff3
ld2410c-builder-fix.log   f4b251fe84be3e0c51b75afcb7eff50155a58ee7532640c33f3347314a3b4a87
env-builder-fix.log       4d6182d04c0be3e621e2ca5b0c1ec72f6625e57a56f7a7cd492a373c1cdd4eea
privacy-sense-matter-room-hub.bin 27aa5ec0aabdbae7b6470830d0a1f4d9ce5e3c43a0c7b6a77220d73d5e616ac1
privacy-sense-matter-room-hub.elf 204ee64f1215c218080c0e3dfd9583cc869ffb2c23de34f1ed2252df4c13dea8
```

## Flash status

The Windows USB/IP attach was later confirmed by the WSL kernel:

- CP2102 was attached to USB/IP and the `cp210x` driver bound to `ttyUSB0` (major/minor `188:0`).
- The Codex execution environment exposed `/sys/class/tty/ttyUSB0`, but its `/dev` mount is read-only and did not create `/dev/ttyUSB0`.
- `udevadm trigger` was denied by the restricted environment, and creating a temporary `188:0` device node with `mknod` was also denied, including the escalated attempt.
- A first flash invocation was stopped during the unintended `ninja flash` rebuild at `845/1480`; it never reached `esptool` or serial I/O. No bytes were written to the board. Its partial log is outside the repository at:

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/flash.log
```

The device node was subsequently made available by the user's USB/IP attach. The successful flash used:

```text
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export IDF_CCACHE_ENABLE=1
export CCACHE_DIR=/tmp/psrh-042-matter-v15-verification.KBATZe/ccache
export NINJAFLAGS=-j2
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build -p /dev/ttyUSB0 flash
```

Result: **PASS**, command exit code `0`. `esptool.py v4.12.0` identified an ESP32-C6 (revision `v0.2`) on `/dev/ttyUSB0`; the bootloader, application image, and partition table each reported `Hash of data verified`, followed by `Hard resetting via RTS pin...` and `Done`. The application image written was `1,901,888` bytes and matches the verified build artifact SHA-256 listed above: `27aa5ec0aabdbae7b6470830d0a1f4d9ce5e3c43a0c7b6a77220d73d5e616ac1`.

The successful flash output was captured from the user's WSL terminal. The log is outside the repository:

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/flash-20260807_161456.log
```

SHA-256: `c86cee5cc7f24e4f34959682d6985eb6998459c7fe738451236cb7376eeb6014`.

The log records `esptool.py v4.12.0`, ESP32-C6 revision `v0.2`, four verified writes (bootloader, application image, partition table, and OTA data), `Hard resetting via RTS pin...`, and `Done`. The device was already commissioned before this flash, so commissioning was intentionally not repeated and no factory reset or NVS erase was performed.

A non-destructive post-flash monitor attempt was made with `--no-reset`:

```text
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build -p /dev/ttyUSB0 monitor --no-reset --timestamps
```

The monitor could not open the port because `/dev/ttyUSB0` disappeared from the Codex execution environment after the board reset, while `/sys/class/tty/ttyUSB0` still reported major/minor `188:0`. No device state was changed. The captured monitor-attempt log is outside the repository:

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/serial-monitor-20260807.log
```

SHA-256: `064fe125abdb8140d1977611ee37d38711cf361d820e516e818f0cd0781d3a2b`.

The user's subsequent monitor run captured a normal post-flash reboot and is outside the repository:

```text
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build -p /dev/ttyUSB0 monitor --timestamps
/tmp/psrh-042-matter-v15-verification.KBATZe/monitor-20260807_162032.log
```

Monitor log SHA-256: `436f14ec99f07f628481f7d7f07fb846d6b03726934e81366c27ae7f788d95e8`.

### Observed post-flash behavior

- **PASS:** normal ESP32-C6 reboot and application boot; `saved credentials found`; Matter Fabric was retrieved from storage and reported `Fabric already commissioned`; BLE advertisement was disabled; Matter stack started with EP0+EP1+EP2; Wi-Fi reached `STA got IP`; IPv4 was assigned; operational Matter mDNS was published; Matter server initialization completed; BLE memory was reclaimed; radar/environment sensors became online; state-machine, radar, UI, and sensor heartbeats continued without panic or watchdog reset.
- **Warning:** `config: NVS config missing/size mismatch; applying defaults` was emitted for the application `ps_cfg` namespace. This is separate from the preserved Wi-Fi credentials and Matter Fabric, which were both recovered successfully.
- **Failure requiring follow-up:** twice, EP1 Occupancy attribute path `0x1/0x406/0x0` failed with `err: 258` (`ESP_ERR_INVALID_ARG`) while occupancy changed. The locked ESP-Matter v1.5 data model creates this attribute as `esp_matter_bitmap8(...)` (`esp-matter/components/esp_matter/data_model/esp_matter_attribute.cpp:3070-3073`), but the current application update path at `firmware/main/matter_app.cpp:470` supplies `esp_matter_uint8(...)`. The SDK requires an exact value-type match, so the application ignores a failed `attribute::update()` and the EP1 Matter occupancy value is not reliably synchronized. The `matter_app.cpp:470` line is unchanged from baseline `02e67aa`; this is a pre-existing defect exposed by the hardware run, not a silent pass.

The first hardware result was therefore **conditional**: commissioned-state recovery, Matter server, and runtime health passed, while EP1 occupancy synchronization exposed the type defect documented below. No factory reset or re-commissioning was performed.

## Builder fix iteration

The Builder corrected the exact Occupancy value type in `firmware/main/matter_app.cpp`:

```diff
- esp_matter_attr_val_t val = esp_matter_uint8(occ_val);
+ esp_matter_attr_val_t val = esp_matter_bitmap8(occ_val);
```

This is an attribute value-type correction only; EP0/EP1/EP2 topology, cluster IDs, mode values, and the approved Radar FeatureMap were unchanged.

### Reverification artifacts

| Artifact | Result / SHA-256 |
|---|---|
| Final clean ESP32-C6 build log | PASS, Ninja `1497/1497`; `5d6fc6cc23259041a687f2d57d34e5e7dda0f24f36f8f9b431d19c044ccc8ff5` |
| Final `idf.py size` log | PASS; `c85c7312b6f98ca3022c413db261b7b076a78af8c5272ae22cd559570cf5cf2f` |
| Final application binary | 1,901,888 B; `1aff0d35f1ffa84119d30656e47770b3bda67ceaba4f57d91bc685561deb8444` |
| Final ELF | `6acd7b0590655c2c08285c72f2075bcd821b97449de77ea7f8044b0ccb0ed7dc` |
| Final host tests | 178/178 PASS; host `5c311ab6eaa49596b8788d3bb9bcfad14081ac3ae64b672c41c595014660fff3`, LD2410C `f4b251fe84be3e0c51b75afcb7eff50155a58ee7532640c33f3347314a3b4a87`, DHT22 `4d6182d04c0be3e621e2ca5b0c1ec72f6625e57a56f7a7cd492a373c1cdd4eea` |
| Builder fix flash log | PASS, exit `0`, four verified writes; `3df851108047ea31bc6c97b038fc8b6acc3d8cedebd0b984174f56f23b85c610` |
| Builder fix monitor log | `3750a16f40246fec34d52022befcd2083815f0ea58175397d7d564ecbb25a4f0` |
| Builder fix network monitor (`--no-reset`) | PASS, connected runtime `st=2`; `8d9ff4e3033d4eb592b05cfe29bb076b6b8b81c8cdc58f80a8f6574c80ec19e2` |
| Builder fix network monitor (normal reset) | PASS, final boot reached IP/Matter operational mDNS and ran heartbeats; `9292598cb694df21fca3cbeeab14b37fb8ee4a33575fe8bdf7bade13ee671cb9` |

The final monitor showed `Fabric already commissioned`, saved Wi-Fi credentials, Matter stack startup, and EP1 occupancy value `1` without any subsequent `Failed to set attribute value` or `err: 258`. The runtime tasks and heartbeats continued without panic or watchdog reset.

That earlier monitor window did not reach `STA got IP`: Wi-Fi repeatedly reported reason `201` (`WIFI_REASON_NO_AP_FOUND`) and ESP-Matter reported `0x0500300F` while updating network status. This was an AP-visibility/connectivity gap, not a commissioning or NVS erase event; the stored Fabric and credentials were still present. The final network/Matter operational retest is recorded below. The `ps_cfg` missing/size-mismatch warning remains a separate existing application-config issue.

### Final network recovery retest

After the configured AP became visible, the Builder performed one normal monitor reset against the final flashed image. No NVS erase, factory reset, or commissioning action was used:

```text
timeout --foreground -s INT -k 5 35s idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build -p /dev/ttyUSB0 monitor --timestamps
```

Result: **PASS**. The final image reported saved credentials and an already commissioned Fabric, started EP0+EP1+EP2, retried the initial Wi-Fi attempt, then reached `STA got IP`, IPv4 connectivity, and operational Matter mDNS publication (`_matter._tcp`). The network state later reported `st=2` (`NET_SM_STATE_CONNECTED`), and state-machine, UI, environment, radar, and network heartbeats continued through the capture. EP1 Occupancy attribute value `1` was emitted, with no `Failed to set attribute value` and no `err:258` in the final log. There was no panic, abort, watchdog reset, or task failure; the monitor wrapper ended with timeout signal `137` only because the bounded capture was intentionally stopped.

Raw log (outside the repository):

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/monitor-builder-fix-network-reset-20260807.log
```

SHA-256: `9292598cb694df21fca3cbeeab14b37fb8ee4a33575fe8bdf7bade13ee671cb9`.

## ChangeToMode NIGHT policy fix

The original asynchronous path allowed `state_machine.c` to apply `MODE_NIGHT`
without checking the configured local-time window. This violated
`docs/matter-data-model.md` §4.4: an out-of-window NIGHT command must fail,
not return controller success while the local constraint is ignored.

The Builder fixed the protocol boundary and retained a state-machine defense:

- `firmware/main/matter_app.cpp:102-137` fails closed unless SNTP time is
  valid, `config_get()` succeeds, and the current local minute is inside the
  configured window. `room_supported_modes_manager::getModeOptionByMode()`
  returns `InteractionModel::Status::Failure` for out-of-window NIGHT before
  `CurrentMode::Set()` can run (`matter_app.cpp:175-193`).
- `firmware/main/state_machine.c:660-696` re-evaluates the same pure
  `night_window_sm` policy after the event-queue handoff and refuses a direct
  invalid NIGHT state transition.
- The locked ESP-Matter server confirms this ordering in
  `connectedhomeip/src/app/clusters/mode-select-server/mode-select-server.cpp:116-137`:
  it calls `SupportedModesManager::getModeOptionByMode()` and returns its
  failure status before setting `CurrentMode`.
- No endpoint, cluster, attribute, mode value, or approved Radar FeatureMap
  was changed by this policy fix.

### Policy-fix machine verification

Host tests were rerun after the policy change with the existing `-j2` limit:

```text
make -C tests/host BUILD=/tmp/psrh-042-matter-v15-verification.KBATZe/tests-host-policy-fix -j2 all
make -C firmware/components/ld2410c/test clean
make -C firmware/components/ld2410c/test -j2 test_all
make -C firmware/components/env_sensor/test clean
make -C firmware/components/env_sensor/test -j2 test
```

Result: **PASS**, project host `127/127`, LD2410C `40/40`, DHT22 `11/11`,
total **178/178 PASS**. Logs and SHA-256:

```text
host-policy-fix.log    63115ed709720224a8ae2c96a9107b3d36bdf00566e9e110a4c176914cbb4919
ld2410c-policy-fix.log f4b251fe84be3e0c51b75afcb7eff50155a58ee7532640c33f3347314a3b4a87
env-policy-fix.log     0cc426439b886a100de7c82e16090153edf3370a754dab9b85be7c0e3fe18704
```

A fresh isolated ESP32-C6 build was then performed with `NINJAFLAGS=-j2`:

```text
source /home/administrator/esp/esp-idf/export.sh
source /home/administrator/esp/esp-matter/export.sh
export ESP_MATTER_PATH=/home/administrator/esp/esp-matter
export IDF_CCACHE_ENABLE=1
export CCACHE_DIR=/tmp/psrh-042-matter-v15-verification.KBATZe/ccache
export NINJAFLAGS=-j2
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix set-target esp32c6
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix reconfigure
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix build
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix size
```

Result: **PASS**, Ninja `1497/1497`; the build log SHA-256 is
`28eb22c2170e21f400d5c8c9ef194051e05221897141ef4750ed85030ffd488b`.
The policy-fix size log SHA-256 is
`8f0b429d5b132465c9c1c214baeb81bbba2300983014a2961d644da0991d9ebf`.

| Resource | Policy-fix result |
|---|---:|
| Flash Code | 1,749,534 B |
| Application binary | 1,902,416 B (`0x1d0750`); app partition `0x32f8b0` / 64% free |
| `idf.py size` total image | 1,902,319 B |
| DIRAM | 241,581 B (53.43%); 210,531 B remaining |
| LP SRAM | 24 B (0.15%); 16,360 B remaining |
| Bootloader | 22,128 B (`0x5670`); 32% free |

```text
privacy-sense-matter-room-hub.bin  58b19591999b98192770742a4de53b5b66ba15ae623213fbf2eaac3fff331aef
privacy-sense-matter-room-hub.elf  38534f53805fbdbb5c98a1b454e6b3348cd279fb09129cccf9e6ee7448584c89
```

The policy-fix image was flashed without erasing NVS:

```text
idf.py -B /tmp/psrh-042-matter-v15-verification.KBATZe/firmware-build-policy-fix -p /dev/ttyUSB0 flash
```

Result: **PASS**, exit `0`; all four writes reported verified hashes and the
board hard-reset. Raw flash log:

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/flash-policy-fix-20260807.log
SHA-256: 4422a51d1c7b036c855e503b0cb7e9410f45ae7265caa83bd719eced8f838a0a
```

The post-flash serial capture used a pseudo-TTY because `idf_monitor` requires
TTY input. It reported saved Wi-Fi credentials, an already commissioned
Fabric, EP0+EP1+EP2, `STA got IP`, IPv4 connectivity, operational `_matter._tcp`
mDNS, `SNTP: time synced`, and continued state-machine/radar/UI heartbeats.
No panic, abort, watchdog reset, `Failed to set attribute value`, or `err: 258`
was observed. The bounded monitor ended with timeout exit `124` by design.

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/monitor-policy-fix-boot-20260807.log
SHA-256: cdc398b0f52ddfcb2abd2db9e3ccaa2b520176e4f6e06c356702b6d99ca8c698
```

### Controller-command HIL status

The local `chip-tool` binary exists, but its standard persistent storage was
empty and no existing controller Fabric credentials were available in the
workspace. A read-only attempt was made with an isolated storage directory:

```text
/home/administrator/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool \
  modeselect read current-mode 0x1234 2 \
  --storage-directory /tmp/psrh-042-matter-v15-verification.KBATZe/chip-tool-policy-fix-storage
```

The controller generated a new local test fabric, then failed operational mDNS
resolution under the WSL NAT network (`Timeout`) before establishing a CASE
session. It did not write any device attribute, commission, factory-reset, or
erase operation. Raw log:

```text
/tmp/psrh-042-matter-v15-verification.KBATZe/chip-tool-policy-fix-read-20260807.log
SHA-256: 1828eca3efc15eb576b3d51e0b64f55920e3e6e5156e395ceaa57f2fd3260228
```

Therefore a real controller `ChangeToMode(NIGHT)` response was **not claimed**
as PASS. The code/build/serial evidence passes, while controller-command HIL
remains pending until an existing Matter controller session/storage is made
available or a human-approved non-destructive commissioning path is provided.

## Evidence retention update (2026-08-08)

The T14/T15 raw commissioning logs referenced by the historical session
summary were stored under `/tmp` and have since been removed. A read-only
search of the host did not find the corresponding `chip_tool_config*.ini`
Fabric storage either. The historical results above are retained as a summary,
but the raw log files and their SHA-256 values are no longer reproducible.

No T14, T09, T10, T12, or T15 rerun was performed. No new commissioning,
factory reset, NVS erase, or flash operation was performed during this update.
