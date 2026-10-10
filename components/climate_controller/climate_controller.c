#include "climate_controller.h"
#include "dht22_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fan_actuator.h"
#include "mq135_sensor.h"
#include <math.h>

#define TEMPERATURE_ON_LOW 26.0f
#define TEMPERATURE_ON_MEDIUM 27.0f
#define TEMPERATURE_ON_FAST 28.0f
#define HUMIDITY_ON_LOW 55.0f
#define HUMIDITY_ON_MEDIUM 60.0f
#define HUMIDITY_ON_FAST 65.0f
#define GAS_ON_LOW 1800
#define GAS_ON_MEDIUM 2400
#define GAS_ON_FAST 3000
#define TEMPERATURE_HYSTERESIS 0.5f
#define HUMIDITY_HYSTERESIS 2.0f
#define GAS_HYSTERESIS 150
#define DHT22_STALE_WARN_MS 15000

static const char *TAG = "climate_controller";
static float s_temperature;
static float s_humidity;
static int s_gas_raw;
static bool s_have_temperature;
static bool s_have_humidity;
static bool s_have_gas;
static int s_level_temperature;
static int s_level_humidity;
static int s_level_gas;
static int64_t s_dht22_sensor_last_ok_us;
static fan_actuator_speed_t s_last_applied_speed = FAN_ACTUATOR_SPEED_OFF;

static fan_actuator_speed_t higher_speed(fan_actuator_speed_t a, fan_actuator_speed_t b)
{
    return (a > b) ? a : b;
}

static int level_of(int current, const int on_threshold[3], int hysteresis, int held_level)
{
    int level = 0;
    for (int i = FAN_ACTUATOR_SPEED_LOW; i <= FAN_ACTUATOR_SPEED_HIGH; ++i)
    {
        if (current >= on_threshold[i - 1])
        {
            level = i;
        }
    }
    if (level < held_level && current >= on_threshold[held_level - 1] - hysteresis)
    {
        level = held_level;
    }
    return level;
}

static void evaluate_and_apply_fan_speed(void)
{
    if (!s_have_temperature || !s_have_humidity || !s_have_gas)
    {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if ((now_us - s_dht22_sensor_last_ok_us) / 1000 > DHT22_STALE_WARN_MS)
    {
        ESP_LOGW(TAG, "No valid DHT22 reading for %lld s; keeping current speed",
                 (long long)((now_us - s_dht22_sensor_last_ok_us) / 1000000));
        s_dht22_sensor_last_ok_us = now_us;
    }
    static const int temp_levels[3] = {(int)(TEMPERATURE_ON_LOW * 10), (int)(TEMPERATURE_ON_MEDIUM * 10), (int)(TEMPERATURE_ON_FAST * 10)};
    static const int hum_levels[3] = {(int)HUMIDITY_ON_LOW, (int)HUMIDITY_ON_MEDIUM, (int)HUMIDITY_ON_FAST};
    static const int gas_levels[3] = {GAS_ON_LOW, GAS_ON_MEDIUM, GAS_ON_FAST};
    s_level_temperature = level_of((int)(s_temperature * 10.0f), temp_levels, (int)(TEMPERATURE_HYSTERESIS * 10), s_level_temperature);
    s_level_humidity = level_of((int)(s_humidity + 0.5f), hum_levels, (int)HUMIDITY_HYSTERESIS, s_level_humidity);
    s_level_gas = level_of(s_gas_raw, gas_levels, GAS_HYSTERESIS, s_level_gas);
    fan_actuator_speed_t speed = (fan_actuator_speed_t)higher_speed(higher_speed(s_level_temperature, s_level_humidity), s_level_gas);
    const bool speed_changed = speed != s_last_applied_speed;
    if (speed_changed)
    {
        ESP_LOGI(TAG, "Climate: T=%.1fC H=%.1f%% gas=%d -> fan=%d",
                 s_temperature, s_humidity, s_gas_raw, speed);
    }
    if (speed_changed && fan_actuator_set_speed(speed) == ESP_OK)
    {
        s_last_applied_speed = speed;
    }
}

static void on_dht22_sensor_reading(const dht22_sensor_reading_t *reading, void *context)
{
    (void)context;
    if (!isfinite(reading->temperature) || !isfinite(reading->humidity) ||
        reading->humidity < 0.0f || reading->humidity > 100.0f ||
        reading->temperature < -40.0f || reading->temperature > 80.0f)
    {
        ESP_LOGW(TAG, "DHT22 gave implausible values (T=%.1f H=%.1f), ignoring", reading->temperature, reading->humidity);
        return;
    }
    s_temperature = reading->temperature;
    s_humidity = reading->humidity;
    s_have_temperature = true;
    s_have_humidity = true;
    s_dht22_sensor_last_ok_us = esp_timer_get_time();
    evaluate_and_apply_fan_speed();
}

static void on_mq135_sensor_reading(const mq135_sensor_reading_t *reading, void *context)
{
    (void)context;
    s_gas_raw = reading->raw_value;
    s_have_gas = true;
    evaluate_and_apply_fan_speed();
}

esp_err_t climate_controller_init(void)
{
    esp_err_t err = fan_actuator_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "fan_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = dht22_sensor_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "dht22_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    dht22_sensor_set_reading_callback(on_dht22_sensor_reading, NULL);
    err = mq135_sensor_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mq135_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    mq135_sensor_set_reading_callback(on_mq135_sensor_reading, NULL);
    ESP_LOGI(TAG, "Climate controller initialized");
    return ESP_OK;
}