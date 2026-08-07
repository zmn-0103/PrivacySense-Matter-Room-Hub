# PrivacySense Matter Room Hub - Top-level CMakeLists.txt
#
# ESP-IDF + ESP-Matter project entry point.
# Toolchain baseline: ESP-IDF v5.4.1, ESP-Matter release/v1.5 (see firmware/README.md).
#
# Build (with esp-idf + esp-matter environment loaded):
#   source <esp-idf>/export.sh
#   source <esp-matter>/export.sh
#   export ESP_MATTER_PATH=<esp-matter>
#   export NINJAFLAGS=-j2
#   idf.py -B <build-dir> set-target esp32c6
#   idf.py -B <build-dir> reconfigure
#   ninja -C <build-dir> -j2 all
#   idf.py -B <build-dir> size
#
# ESP_MATTER_PATH is exported by the ESP-Matter environment script.
#
# Boilerplate mirrors esp-matter release/v1.5 examples/light/CMakeLists.txt.
# Project-local components live under firmware/components/ and are appended
# to EXTRA_COMPONENT_DIRS after the esp-matter device/Matter SDK dirs.

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR
        "ESP_MATTER_PATH environment variable is not set. "
        "Run the ESP-Matter export script before invoking idf.py.")
endif()

# v1 locks the board to ESP32-C6. If a previous configure run failed mid-way
# (e.g. missing dependencies), IDF_TARGET can be empty when `idf.py build` is
# re-invoked without `set-target` — ESP-IDF's target-aware logic would then
# run before the target is set. Default to esp32c6 here and reject anything
# else, so the build directory is always in a consistent target state.
if(NOT DEFINED IDF_TARGET OR "${IDF_TARGET}" STREQUAL "")
    set(IDF_TARGET "esp32c6")
endif()

if(NOT "${IDF_TARGET}" STREQUAL "esp32c6")
    message(FATAL_ERROR
        "Unsupported IDF_TARGET: ${IDF_TARGET}. v1 locks the board to esp32c6.")
endif()

# Device HAL path: target-specific (ESP32-C6 → C6 devkit). Set unconditionally
# now that IDF_TARGET is guaranteed to be esp32c6.
set(ENV{ESP_MATTER_DEVICE_PATH}
    "$ENV{ESP_MATTER_PATH}/device_hal/device/esp32c6_devkit_c")

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# project.cmake MUST be included before any ESP-IDF target-aware logic.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

# EXTRA_COMPONENT_DIRS: esp-matter official dirs first, then project-local.
#
# NOTE on what is INTENTIONALLY NOT listed here:
#   - ${ESP_MATTER_PATH}/examples/common
#       Contains app_reset, which REQUIRES button (from button_driver) →
#       pulls in the device HAL's led_driver → espressif/led_strip ^1.0.0
#       (legacy driver/rmt.h). The project does not use app_reset; including
#       the whole examples/common directory drags in this transitive
#       dependency and breaks the build. The include() of
#       examples/common/cmake_common/components_include.cmake above is still
#       kept — that file only adds Matter SDK component dirs, not app_reset.
#   - ${ESP_MATTER_PATH}/device_hal/device
#       Same root cause: device_hal/device contains app_reset and the
#       button/led drivers with the same legacy led_strip constraint.
#   - ${extra_components_dirs_append}
#       Populated by esp_matter_device.cmake above with the device HAL's
#       led_driver / button_driver subdirs; same legacy led_strip constraint.
#
# Rationale in one line: led_strip 1.x (legacy driver/rmt.h) conflicts with
# env_sensor's driver/rmt_rx.h (new RMT driver) and triggers the early
# abort(): "CONFLICT! driver_ng is not allowed to be used with the legacy
# driver". The project pins espressif/led_strip ^2.0.0 via
# main/idf_component.yml and manages LED/button itself (firmware/main/ui.c,
# firmware/main/button.c), so the device HAL's LED/button abstractions are
# not needed.
#
# The esp_matter_device.cmake include above is kept because it may set
# other device-HAL variables consumed by esp_matter core; only the
# device_hal/device component directory is excluded from EXTRA_COMPONENT_DIRS.
#
# Project-local components (ld2410c, env_sensor) are kept last so they cannot
# accidentally shadow esp-matter component names.
set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${CMAKE_CURRENT_LIST_DIR}/components")

project(privacy-sense-matter-room-hub)

# Match upstream light example: C++17, optimised, CHIP_HAVE_CONFIG_H.
# Keep compiler diagnostics visible in this project-owned build file. The
# initial build does not promote all upstream warnings to errors; any emitted
# warnings are classified in the Phase 5 evidence instead of being suppressed.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
