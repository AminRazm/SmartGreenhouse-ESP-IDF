
idf_component_register(SRCS "climate_manager.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES "fan" "mq135" "dht22" "esp_timer")
