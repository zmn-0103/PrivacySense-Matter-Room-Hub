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
