idf_component_register(
    SRCS "yl69_sensor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_adc shared_adc esp_driver_gpio
)
