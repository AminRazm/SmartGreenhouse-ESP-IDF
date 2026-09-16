idf_component_register(
    SRCS "irrigation_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES moisture pump led esp_timer esp_adc shared_adc esp_driver_gpio
)
