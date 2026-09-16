idf_component_register(
    SRCS "bh1750.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2c
)