idf_component_register(
    SRCS "lighting_controller.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bh1750_sensor light_actuator
)
