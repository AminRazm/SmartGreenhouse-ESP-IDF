idf_component_register(
    SRCS "irrigation_controller.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES soil_moisture_sensor pump_actuator indicator esp_timer
)
