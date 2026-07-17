# PrivacySense Matter Room Hub - Top-level CMakeLists.txt
#
# ESP-IDF + ESP-Matter project entry point.
# Toolchain baseline: ESP-IDF v5.4.1, ESP-Matter release/v1.5 (see firmware/README.md).
#
# Build (inside WSL2 with esp-idf + esp-matter env loaded):
#   idf.py -B /root/build/privacy-sense set-target esp32c6
#   idf.py -B /root/build/privacy-sense build
#
# ESP_MATTER_PATH is exported by /root/esp/esp-matter/export.sh.
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
        "Run `source /root/esp/esp-matter/export.sh` before invoking idf.py.")
endif()

# Device HAL path is target-specific; ESP32-C6 uses the C6 devkit.
if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    if("${IDF_TARGET}" STREQUAL "esp32c6")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c6_devkit_c)
    else()
        message(FATAL_ERROR "Unsupported IDF_TARGET: ${IDF_TARGET}. v1 locks the board to esp32c6.")
    endif()
endif()

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# project.cmake MUST be included before any ESP-IDF target-aware logic.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

# EXTRA_COMPONENT_DIRS: esp-matter official dirs first, then project-local.
# ${extra_components_dirs_append} is populated by esp_matter_device.cmake above
# and contains target-specific device HAL subdirs (e.g. led_strip, button) for
# the selected devkit. It MUST be listed before the project-local components so
# the device HAL is resolvable. Project-local components (ld2410c, env_sensor)
# are kept last so they cannot accidentally shadow esp-matter component names.
set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append}
    "${CMAKE_CURRENT_LIST_DIR}/components")

project(privacy-sense-matter-room-hub)

# Match upstream light example: C++17, optimised, CHIP_HAVE_CONFIG_H.
# Upstream emits -Wextra warnings; suppress the noisy ones rather than
# enabling -Werror (see firmware/README.md "First build status").
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear
# various flags that depend on -Wformat.
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
