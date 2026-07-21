# PrivacySense Matter Room Hub - LD2410C radar driver component
#
# Project-local component. Provides UART1 driver + parser + sensor_radar_task.
# Parsed frames are delivered via a caller-registered callback (the driver
# does NOT hold any FreeRTOS queue handle).
#
# R08 fault injection: pass -DLD2410C_FAULT_INJECT=ON to cmake to enable
# the stop-timeout fault injection (affects only this component).
# Example:
#   idf.py -B build-r08 reconfigure build -DLD2410C_FAULT_INJECT=ON

idf_component_register(
    SRCS
        "ld2410c.c"
        "ld2410c_parser.c"
        "ld2410c_core.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "."       # for ld2410c_parser.h / ld2410c_core.h (private)
    REQUIRES
        driver
        esp_timer
)

# Cache variable: OFF by default, set to ON via -DLD2410C_FAULT_INJECT=ON
set(LD2410C_FAULT_INJECT OFF CACHE BOOL "Enable R08 stop-timeout fault injection")
if(LD2410C_FAULT_INJECT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        LD2410C_TEST_FAULT_INJECT_STOP=1)
    message(STATUS "LD2410C: fault injection ENABLED (R08 test)")
endif()
