# PrivacySense Matter Room Hub - LD2410C radar driver component
#
# Project-local component. Provides UART1 driver + parser + sensor_radar_task.
# Parsed frames are delivered via a caller-registered callback (the driver
# does NOT hold any FreeRTOS queue handle).

idf_component_register(
    SRCS
        "ld2410c.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_timer
)
