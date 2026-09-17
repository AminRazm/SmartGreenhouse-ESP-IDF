idf_component_register(
    SRCS "climate_controller.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES fan_actuator dht22_sensor mq135_sensor esp_timer
)
