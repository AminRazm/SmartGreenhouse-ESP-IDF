#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "indicator.h"
#include "wifi_manager.h"
#include "lighting_controller.h"
#include "climate_controller.h"
#include "irrigation_controller.h"

static const char *TAG = "main";

void app_main(void)
{
    if (indicator_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Indicator initialization failed! Restarting...");
        esp_restart();
    }

    if (wifi_manager_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi initialization failed!");
        indicator_set_event(INDICATOR_EVENT_CRITICAL, true);
    }
    
    if (lighting_controller_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Lighting initialization failed!");
        indicator_set_event(INDICATOR_EVENT_CRITICAL, true);
    }

    if (climate_controller_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Climate initialization failed! Restarting...");
        esp_restart();
    }

    if (irrigation_controller_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Irrigation initialization failed! Restarting...");
        esp_restart();
    }
}