# PrivacySense Matter Room Hub - environment sensor component
#
# Project-local component. AM2302 / DHT22 single-wire driver on GPIO2 using
# the ESP-IDF RMT RX peripheral. Owns sensor_env_task. Samples are delivered
# via a caller-registered callback (no queue handle held by this component).

idf_component_register(
    SRCS
        "env_sensor.c"
        "env_sensor_parser.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_timer
)
