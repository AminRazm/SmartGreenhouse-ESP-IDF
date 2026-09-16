#include "climate_manager.h"
#include "esp_err.h"
#include "irrigation_manager.h"
#include "lighting_manager.h"
#include "wifi.h"

void app_main(void)
{
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(climate_manager_init());
    ESP_ERROR_CHECK(lighting_manager_init());
    ESP_ERROR_CHECK(irrigation_manager_init());
}