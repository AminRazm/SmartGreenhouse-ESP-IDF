#include "esp_err.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "climate_controller.h"
#include "irrigation_controller.h"
#include "lighting_controller.h"

#define TIME_SYNC_WAIT_MS 15000

static const char *TAG = "main";

void app_main(void)
{
    if (wifi_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed; startup aborted");
        return;
    }
    if (climate_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "Climate initialization failed; startup aborted");
        return;
    }
    if (irrigation_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "Irrigation initialization failed; startup aborted");
        return;
    }
    if (!wifi_manager_wait_for_time_sync(TIME_SYNC_WAIT_MS)) {
        ESP_LOGW(TAG, "Time is not synchronized yet; lighting will stay OFF until SNTP succeeds");
    }
    if (lighting_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "Lighting initialization failed; startup aborted");
        return;
    }
}