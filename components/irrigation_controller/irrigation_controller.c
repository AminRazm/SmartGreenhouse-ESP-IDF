#include "irrigation_controller.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "status_led_actuator.h"
#include "soil_moisture_sensor.h"
#include "yl69_sensor.h"
#include "pump_actuator.h"




#define SOIL_DRY_THRESHOLD 1300


#define SOIL_OUT_OF_SOIL_THRESHOLD 2400


#define TANK_EMPTY_THRESHOLD 4000




#define IRRIGATION_COOLDOWN_MS (5 * 60 * 1000)

#define TANK_SETTLE_MS      2000 
#define TANK_POLL_MS        1000 
#define TANK_FINAL_DELAY_MS 200  



#define EV_SOIL_DRY BIT0

static const char *TAG = "irrigation_controller";

static EventGroupHandle_t s_events;


static int64_t s_next_allowed_us;


static volatile bool s_cycle_active;


static volatile water_tank_status_t s_tank_status = WATER_TANK_STATUS_UNKNOWN;


static bool s_logged_out_of_soil;
static bool s_logged_cooldown_skip;


static int64_t now_us(void)
{
    return esp_timer_get_time();
}



static void end_cycle(void)
{
    pump_actuator_stop();
    (void)yl69_sensor_set_power(false);

    status_led_actuator_set_pattern(s_tank_status == WATER_TANK_STATUS_EMPTY
                        ? STATUS_LED_PATTERN_TANK_EMPTY
                        : STATUS_LED_PATTERN_OFF);

    s_next_allowed_us = now_us() + (int64_t)IRRIGATION_COOLDOWN_MS * 1000;
    s_cycle_active = false;

    ESP_LOGI(TAG, "irrigation: cycle finished, GPIO18=OFF, next check in %d min",
             IRRIGATION_COOLDOWN_MS / 60000);
}



static void run_irrigation_cycle(void)
{
    s_cycle_active = true;



    (void)yl69_sensor_set_power(true);


    status_led_actuator_set_pattern(STATUS_LED_PATTERN_TANK_EMPTY);

    ESP_LOGI(TAG, "irrigation: soil dry, tank probe ON (GPIO18), settling %d ms", TANK_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(TANK_SETTLE_MS));

    int raw = 0;
    esp_err_t err = yl69_sensor_read_raw(&raw);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "irrigation: tank read failed (%s), cycle cancelled", esp_err_to_name(err));
        end_cycle();
        return;
    }

    ESP_LOGI(TAG, "irrigation: pre-pump raw=%d empty_threshold=%d -> %s",
             raw, TANK_EMPTY_THRESHOLD,
             raw > TANK_EMPTY_THRESHOLD ? "TANK EMPTY" : "water present");

    if (raw > TANK_EMPTY_THRESHOLD) {

        s_tank_status = WATER_TANK_STATUS_EMPTY;
        ESP_LOGW(TAG, "irrigation: water tank is EMPTY (raw=%d), pump not started", raw);

        end_cycle(); 
        return;
    }

    s_tank_status = WATER_TANK_STATUS_HAS_WATER;
    status_led_actuator_set_pattern(STATUS_LED_PATTERN_OFF);



    err = pump_actuator_start_pulse();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "irrigation: pump start failed (%s)", esp_err_to_name(err));
        end_cycle();
        return;
    }

    status_led_actuator_set_pattern(STATUS_LED_PATTERN_PUMP_ACTIVE); 

    while (pump_actuator_is_running()) {

        vTaskDelay(pdMS_TO_TICKS(TANK_POLL_MS));

        if (!pump_actuator_is_running()) {
            break; 
        }

        err = yl69_sensor_read_raw(&raw);

        if (err == ESP_OK && raw > TANK_EMPTY_THRESHOLD) {

            pump_actuator_stop();
            s_tank_status = WATER_TANK_STATUS_EMPTY;

            ESP_LOGE(TAG, "irrigation: tank ran DRY mid-pump (raw=%d), pump stopped immediately", raw);
            break;
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "irrigation: mid-pump tank read failed (%s)", esp_err_to_name(err));
        }
    }



    vTaskDelay(pdMS_TO_TICKS(TANK_FINAL_DELAY_MS));

    err = yl69_sensor_read_raw(&raw);
    if (err == ESP_OK) {

        const bool empty = raw > TANK_EMPTY_THRESHOLD;
        s_tank_status = empty ? WATER_TANK_STATUS_EMPTY : WATER_TANK_STATUS_HAS_WATER;

        ESP_LOGI(TAG, "irrigation: final tank state raw=%d -> %s",
                 raw, empty ? "EMPTY" : "HAS WATER");

    } else {
        ESP_LOGW(TAG, "irrigation: final tank read failed (%s), keeping previous status",
                 esp_err_to_name(err));
    }

    end_cycle();
}



static void irrigation_task(void *arg)
{
    (void)arg;

    for (;;) {


        (void)xEventGroupWaitBits(s_events, EV_SOIL_DRY, pdTRUE, pdFALSE, portMAX_DELAY);

        run_irrigation_cycle();
    }
}



static void on_soil_moisture_reading(const soil_moisture_sensor_reading_t *reading, void *context)
{
    (void)context;

    if (reading->latest_sample >= SOIL_OUT_OF_SOIL_THRESHOLD) {

        if (!s_logged_out_of_soil) {
            ESP_LOGW(TAG, "Soil probe looks OUT OF SOIL (raw=%d), readings ignored",
                     reading->latest_sample);
            s_logged_out_of_soil = true;
        }
        return;
    }
    s_logged_out_of_soil = false;

    if (reading->raw_value < SOIL_DRY_THRESHOLD || s_cycle_active) {
        return; 
    }

    if (now_us() < s_next_allowed_us) {

        if (!s_logged_cooldown_skip) {

            const int64_t remaining_s =
                (s_next_allowed_us - now_us() + 999999) / 1000000;

            ESP_LOGI(TAG, "Soil dry (raw=%d) but cooling down, next check in %lld s",
                     reading->raw_value, (long long)remaining_s);

            s_logged_cooldown_skip = true;
        }
        return;
    }
    s_logged_cooldown_skip = false;


    xEventGroupSetBits(s_events, EV_SOIL_DRY);
}


water_tank_status_t irrigation_controller_get_tank_status(void)
{
    return s_tank_status;
}


esp_err_t irrigation_controller_init(void)
{
    esp_err_t err = pump_actuator_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pump_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = yl69_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "yl69_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }


    err = status_led_actuator_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status_led_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = soil_moisture_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "soil_moisture_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    soil_moisture_sensor_set_reading_callback(on_soil_moisture_reading, NULL);

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreate(irrigation_task, "irrigation", 4096, NULL, 4, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Irrigation initialized: soil_dry=%d, out_of_soil=%d, tank_empty=%d, cooldown=%d min",
             SOIL_DRY_THRESHOLD, SOIL_OUT_OF_SOIL_THRESHOLD,
             TANK_EMPTY_THRESHOLD, IRRIGATION_COOLDOWN_MS / 60000);

    return ESP_OK;
}
