# PrivacySense Matter Room Hub - main component build
#
# Lists all main/ source files and their IDF component dependencies.
# esp_matter is required; ld2410c / env_sensor are project-local components
# under firmware/components/.

idf_component_register(
    SRCS
        "main.c"
        "room_state.c"
        "state_machine.c"
        "button.c"
        "ui.c"
        "network.c"
        "matter_app.c"
        "config.c"
    INCLUDE_DIRS
        "."
    REQUIRES
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
