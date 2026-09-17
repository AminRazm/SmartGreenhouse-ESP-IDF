idf_component_register(
    SRCS "soil_moisture_sensor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_adc shared_adc
)
