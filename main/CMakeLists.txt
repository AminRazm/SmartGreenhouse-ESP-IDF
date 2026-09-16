idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES wifi climate_manager lighting_manager irrigation_manager
)