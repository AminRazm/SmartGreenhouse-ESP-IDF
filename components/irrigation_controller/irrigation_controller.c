#include "irrigation_controller.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "indicator.h"
#include "pump_actuator.h"
#include "soil_moisture_sensor.h"

#define SOIL_DRY_THRESHOLD 1300
// Optional safety guard: set to 4095 to disable it.
#define SOIL_SENSOR_DISCONNECTED_THRESHOLD 2400
#define IRRIGATION_COOLDOWN_MS (15 * 60 * 1000)

static const char *TAG = "irrigation_controller";

static int64_t s_next_allowed_us;
static volatile bool s_cycle_active;
static bool s_sensor_warning_logged;
static TaskHandle_t s_irrigation_task_handle;

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void finish_cycle(void)
{
    pump_actuator_stop();
    indicator_set_event(INDICATOR_EVENT_PUMP_ACTIVE, false);
    s_next_allowed_us = now_us() + (int64_t)IRRIGATION_COOLDOWN_MS * 1000;
    s_cycle_active = false;

    ESP_LOGI(TAG, "Irrigation cycle finished; next check in %d minutes",
             IRRIGATION_COOLDOWN_MS / 60000);
}

static void run_irrigation_cycle(void)
{
    s_cycle_active = true;
    ESP_LOGI(TAG, "Soil is dry; starting a 10-second irrigation cycle");

    const esp_err_t err = pump_actuator_start_pulse();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pump start failed: %s", esp_err_to_name(err));
        finish_cycle();
        return;
    }

    indicator_set_event(INDICATOR_EVENT_PUMP_ACTIVE, true);
    while (pump_actuator_is_running()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    finish_cycle();
}

static void irrigation_task(void *arg)
{
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_irrigation_cycle();
    }
}

static void on_soil_moisture_reading(const soil_moisture_sensor_reading_t *reading,
                                     void *context)
{
    (void)context;

    if (reading->latest_sample >= SOIL_SENSOR_DISCONNECTED_THRESHOLD) {
        indicator_set_event(INDICATOR_EVENT_SOIL_SENSOR_ERROR, true);
        indicator_set_event(INDICATOR_EVENT_SOIL_DRY, false);
        if (!s_sensor_warning_logged) {
            ESP_LOGW(TAG, "Soil sensor reading is unusually high (%d); irrigation skipped",
                     reading->latest_sample);
            s_sensor_warning_logged = true;
        }
        return;
    }
    s_sensor_warning_logged = false;
    indicator_set_event(INDICATOR_EVENT_SOIL_SENSOR_ERROR, false);
    indicator_set_event(INDICATOR_EVENT_SOIL_DRY,
                       reading->raw_value >= SOIL_DRY_THRESHOLD);

    if (reading->raw_value < SOIL_DRY_THRESHOLD || s_cycle_active ||
        now_us() < s_next_allowed_us) {
        return;
    }

    xTaskNotifyGive(s_irrigation_task_handle);
}

esp_err_t irrigation_controller_init(void)
{
    esp_err_t err = pump_actuator_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pump_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = soil_moisture_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "soil_moisture_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    soil_moisture_sensor_set_reading_callback(on_soil_moisture_reading, NULL);

    const BaseType_t created = xTaskCreate(irrigation_task, "irrigation", 3072, NULL, 4,
                                           &s_irrigation_task_handle);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Irrigation initialized: dry<%d, sensor_guard>=%d, cooldown=%d minutes",
             SOIL_DRY_THRESHOLD, SOIL_SENSOR_DISCONNECTED_THRESHOLD,
             IRRIGATION_COOLDOWN_MS / 60000);
    return ESP_OK;
}
