idf_component_register(
    SRCS "mq135_sensor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_adc shared_adc
)
