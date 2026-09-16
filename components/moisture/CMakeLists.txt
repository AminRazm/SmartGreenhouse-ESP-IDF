idf_component_register(
    SRCS "moisture.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_adc shared_adc
)