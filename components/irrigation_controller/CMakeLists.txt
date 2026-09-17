idf_component_register(
    SRCS "irrigation_controller.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES yl69_sensor soil_moisture_sensor pump_actuator status_led_actuator esp_timer
)
