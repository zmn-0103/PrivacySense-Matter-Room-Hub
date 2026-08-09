# PrivacySense Matter Room Hub - main component build
#
# Lists all main/ source files and their IDF component dependencies.
# esp_matter is required; ld2410c / env_sensor are project-local components
# under firmware/components/.

idf_component_register(
    SRCS
        "main.c"
        "room_state.c"
        "occ_sm.c"
        "env_alert_sm.c"
        "night_window_sm.c"
        "network_reconnect_sm.c"
        "state_machine.c"
        "button.c"
        "mode_transition.c"
        "ui.c"
        "ui_rgb.c"
        "ssd1306.c"
        "network.c"
        "health_diag.c"
        "matter_app.cpp"
        "config.c"
        "radar_diag.c"
        "network_diag.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        console
        esp_timer
        esp_event
        esp_hw_support
        esp_system
        heap
        freertos
        log
        nvs_flash
        esp_wifi
        driver
        led_strip
        esp_matter
        ld2410c
        env_sensor
)

# HIL-only timing override. It is disabled by default and must be enabled in a
# dedicated build directory. Release builds reject the override outright, so
# an inherited environment variable or a plain `ninja` invocation can never
# alter production semantics.
option(PSRH_HIL_BUILD "Enable HIL-only test behavior" OFF)
option(PSRH_RELEASE_BUILD "Build production firmware" ON)
set(PSRH_HIL_NIGHT_EXIT_AFTER_MS "" CACHE STRING
    "HIL NIGHT exit delay in milliseconds (1000..3600000; HIL builds only)")

if(PSRH_HIL_BUILD AND PSRH_RELEASE_BUILD)
    message(FATAL_ERROR
        "PSRH_HIL_BUILD is test-only; configure HIL with -DPSRH_RELEASE_BUILD=OFF")
endif()

if(NOT PSRH_HIL_BUILD AND NOT "${PSRH_HIL_NIGHT_EXIT_AFTER_MS}" STREQUAL "")
    message(FATAL_ERROR
        "PSRH_HIL_NIGHT_EXIT_AFTER_MS requires -DPSRH_HIL_BUILD=ON in a dedicated build directory")
endif()

if(PSRH_HIL_BUILD)
    if(NOT PSRH_HIL_NIGHT_EXIT_AFTER_MS MATCHES "^[0-9]+$")
        message(FATAL_ERROR "PSRH_HIL_NIGHT_EXIT_AFTER_MS must be an unsigned integer")
    endif()
    if(PSRH_HIL_NIGHT_EXIT_AFTER_MS LESS 1000 OR
       PSRH_HIL_NIGHT_EXIT_AFTER_MS GREATER 3600000)
        message(FATAL_ERROR
            "PSRH_HIL_NIGHT_EXIT_AFTER_MS must be between 1000 and 3600000 ms")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PSRH_HIL_NIGHT_EXIT_AFTER_MS=${PSRH_HIL_NIGHT_EXIT_AFTER_MS})
    message(STATUS "HIL NIGHT exit override: ${PSRH_HIL_NIGHT_EXIT_AFTER_MS} ms")
endif()
