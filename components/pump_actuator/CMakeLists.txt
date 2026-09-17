idf_component_register(
    SRCS "pump_actuator.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_gpio esp_timer
)
