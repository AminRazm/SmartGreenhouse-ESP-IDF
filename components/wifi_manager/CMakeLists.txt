idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash log esp_timer indicator
)
