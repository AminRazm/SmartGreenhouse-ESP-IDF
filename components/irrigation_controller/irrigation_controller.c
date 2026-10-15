#include "irrigation_controller.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "indicator.h"
#include "pump_actuator.h"
#include "soil_moisture_sensor.h"

#define SOIL_DRY_THRESHOLD 350
#define SOIL_DRY_CONFIRMATIONS 40
#define IRRIGATION_COOLDOWN_MS (15 * 60 * 1000)

#define SOIL_OUT_OF_SOIL_THRESHOLD 450
#define SOIL_OUT_OF_SOIL_CLEAR_THRESHOLD 430
#define SOIL_DISCONNECTED_THRESHOLD 20
#define SOIL_PROBE_STATE_CONFIRMATIONS 3

static const char *TAG = "irrigation_controller";
static int64_t s_next_allowed_us;
static volatile bool s_cycle_active;
static volatile uint16_t s_dry_readings;
static TaskHandle_t s_irrigation_task_handle;

typedef enum
{
    PROBE_OK = 0,
    PROBE_OUT_OF_SOIL,
    PROBE_DISCONNECTED,
} probe_state_t;
static probe_state_t s_probe_state = PROBE_OK;
static uint8_t s_probe_change_readings;

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
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Pump start failed: %s", esp_err_to_name(err));
        finish_cycle();
        return;
    }
    indicator_set_event(INDICATOR_EVENT_PUMP_ACTIVE, true);
    while (pump_actuator_is_running())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    finish_cycle();
}

static void irrigation_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_dry_readings >= SOIL_DRY_CONFIRMATIONS && !s_cycle_active &&
            now_us() >= s_next_allowed_us)
        {
            run_irrigation_cycle();
        }
    }
}

static probe_state_t classify_probe(int value)
{
    if (value < SOIL_DISCONNECTED_THRESHOLD)
    {
        return PROBE_DISCONNECTED;
    }
    if (value >= SOIL_OUT_OF_SOIL_THRESHOLD)
    {
        return PROBE_OUT_OF_SOIL;
    }
    if (value >= SOIL_OUT_OF_SOIL_CLEAR_THRESHOLD && s_probe_state == PROBE_OUT_OF_SOIL)
    {
        return PROBE_OUT_OF_SOIL;
    }
    return PROBE_OK;
}

static bool update_probe_state(int value)
{
    const probe_state_t seen = classify_probe(value);
    if (seen == s_probe_state)
    {
        s_probe_change_readings = 0;
    }
    else if (++s_probe_change_readings >= SOIL_PROBE_STATE_CONFIRMATIONS)
    {
        s_probe_change_readings = 0;
        s_probe_state = seen;
        s_dry_readings = 0;
        indicator_set_event(INDICATOR_EVENT_SOIL_PROBE_OUT_OF_SOIL, seen == PROBE_OUT_OF_SOIL);
        indicator_set_event(INDICATOR_EVENT_SOIL_PROBE_DISCONNECTED, seen == PROBE_DISCONNECTED);
        if (seen == PROBE_OUT_OF_SOIL)
        {
            indicator_set_event(INDICATOR_EVENT_SOIL_DRY, false);
            ESP_LOGW(TAG, "Soil probe is OUT OF SOIL (value=%d); irrigation paused", value);
        }
        else if (seen == PROBE_DISCONNECTED)
        {
            indicator_set_event(INDICATOR_EVENT_SOIL_DRY, false);
            ESP_LOGW(TAG, "Soil probe is NOT CONNECTED (value=%d); irrigation paused", value);
        }
        else
        {
            ESP_LOGI(TAG, "Soil probe is back in the soil (value=%d)", value);
        }
    }
    return s_probe_state == PROBE_OK;
}

static void on_soil_moisture_reading(const soil_moisture_sensor_reading_t *reading,
                                     void *context)
{
    (void)context;
    if (!update_probe_state(reading->average_value))
    {
        return;
    }
    if (reading->average_value >= SOIL_DRY_THRESHOLD)
    {
        if (s_dry_readings < SOIL_DRY_CONFIRMATIONS)
        {
            s_dry_readings++;
        }
    }
    else
    {
        s_dry_readings = 0;
    }
    const bool soil_is_dry = s_dry_readings >= SOIL_DRY_CONFIRMATIONS;
    indicator_set_event(INDICATOR_EVENT_SOIL_DRY, soil_is_dry);
    if (!soil_is_dry || s_cycle_active || now_us() < s_next_allowed_us)
    {
        return;
    }
    xTaskNotifyGive(s_irrigation_task_handle);
}

esp_err_t irrigation_controller_init(void)
{
    esp_err_t err = pump_actuator_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pump_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = soil_moisture_sensor_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "soil_moisture_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    const BaseType_t created = xTaskCreate(irrigation_task, "irrigation", 3072, NULL, 4,
                                           &s_irrigation_task_handle);
    if (created != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    soil_moisture_sensor_set_reading_callback(on_soil_moisture_reading, NULL);
    ESP_LOGI(TAG, "Irrigation initialized: dry>=%d for %d averaged readings, cooldown=%d minutes",
             SOIL_DRY_THRESHOLD, SOIL_DRY_CONFIRMATIONS,
             IRRIGATION_COOLDOWN_MS / 60000);
    return ESP_OK;
}