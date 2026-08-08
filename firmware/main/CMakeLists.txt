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
        nvs_flash
        esp_wifi
        driver
        led_strip
        esp_matter
        ld2410c
        env_sensor
)

# Optional HIL-only timing override. The default build keeps the production
# NIGHT window semantics (22:00–07:00). A separate HIL build may set
# PSRH_HIL_NIGHT_EXIT_AFTER_MS in the environment to exercise the automatic
# exit path without waiting for the real wall-clock boundary.
if(DEFINED ENV{PSRH_HIL_NIGHT_EXIT_AFTER_MS} AND
   NOT "$ENV{PSRH_HIL_NIGHT_EXIT_AFTER_MS}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PSRH_HIL_NIGHT_EXIT_AFTER_MS=$ENV{PSRH_HIL_NIGHT_EXIT_AFTER_MS})
    message(STATUS "HIL NIGHT exit override: $ENV{PSRH_HIL_NIGHT_EXIT_AFTER_MS} ms")
endif()
