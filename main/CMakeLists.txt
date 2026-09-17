idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES wifi_manager climate_controller lighting_controller irrigation_controller
)
