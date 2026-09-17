idf_component_register(
    SRCS "status_led_actuator.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_rmt esp_driver_gpio
)
