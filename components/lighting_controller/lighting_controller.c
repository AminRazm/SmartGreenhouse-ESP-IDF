#include "lighting_controller.h"
#include "bh1750_sensor.h"
#include "esp_log.h"
#include "light_actuator.h"
#include <stdbool.h>
#include <time.h>

#define TARGET_LUX 4000.0f
#define ACTIVE_HOUR_START 6
#define ACTIVE_HOUR_END 21
#define LUX_DEADBAND_RATIO 0.05f
#define LUX_EMA_ALPHA 0.5f
#define STEP_DIVISOR 10.0f
#define STEP_MIN 1
#define STEP_MAX 204
#define LIGHT_LEVEL_MAX 1023
#define MIN_VALID_EPOCH ((time_t)1600000000)

static const char *TAG = "lighting_controller";

typedef struct
{
    int level;
    float lux_ema;
    bool has_time;
} lighting_state_t;

static lighting_state_t s_state = {
    .level = 0,
    .lux_ema = -1.0f,
    .has_time = false,
};

static bool get_valid_local_time(struct tm *local_time)
{
    const time_t now = time(NULL);
    return now >= MIN_VALID_EPOCH && localtime_r(&now, local_time) != NULL;
}

static int calculate_step(float lux)
{
    const float error = (lux > TARGET_LUX)
                            ? lux - TARGET_LUX
                            : TARGET_LUX - lux;
    int step = (int)(error / STEP_DIVISOR);
    if (step < STEP_MIN)
    {
        step = STEP_MIN;
    }
    else if (step > STEP_MAX)
    {
        step = STEP_MAX;
    }
    return step;
}

static void set_light_level(int level, float lux)
{
    if (level == s_state.level)
    {
        return;
    }
    const esp_err_t err = light_actuator_set_level(level);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "light_actuator_set_level failed: %s", esp_err_to_name(err));
        return;
    }
    s_state.level = level;
    ESP_LOGI(TAG, "Lux=%.0f, light level=%d", lux, level);
}

static void control_light(float lux)
{
    if (s_state.lux_ema < 0.0f)
    {
        s_state.lux_ema = lux;
    }
    else
    {
        s_state.lux_ema += LUX_EMA_ALPHA * (lux - s_state.lux_ema);
    }
    const float smoothed_lux = s_state.lux_ema;
    struct tm local_time;
    const bool has_time = get_valid_local_time(&local_time);
    if (has_time != s_state.has_time)
    {
        s_state.has_time = has_time;

        if (has_time)
        {
            ESP_LOGI(TAG, "Clock available; light schedule is active (%02d:00-%02d:00)",
                     ACTIVE_HOUR_START, ACTIVE_HOUR_END);
        }
    }
    if (has_time &&
        (local_time.tm_hour < ACTIVE_HOUR_START ||
         local_time.tm_hour >= ACTIVE_HOUR_END))
    {
        set_light_level(0, smoothed_lux);
        return;
    }
    const float deadband = TARGET_LUX * LUX_DEADBAND_RATIO;
    if (smoothed_lux > TARGET_LUX - deadband &&
        smoothed_lux < TARGET_LUX + deadband)
    {
        return;
    }
    const int step = calculate_step(smoothed_lux);
    int new_level = s_state.level;
    if (smoothed_lux < TARGET_LUX)
    {
        new_level += step;
        if (new_level > LIGHT_LEVEL_MAX)
        {
            new_level = LIGHT_LEVEL_MAX;
        }
    }
    else
    {
        new_level -= step;
        if (new_level < 0)
        {
            new_level = 0;
        }
    }
    set_light_level(new_level, smoothed_lux);
}

static void on_light_reading(const bh1750_sensor_reading_t *reading, void *context)
{
    (void)context;
    control_light(reading->lux);
}

esp_err_t lighting_controller_init(void)
{
    esp_err_t err = light_actuator_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "light_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = bh1750_sensor_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "bh1750_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    bh1750_sensor_set_reading_callback(on_light_reading, NULL);
    ESP_LOGI(TAG, "Lighting controller initialized (target=%.0f lux)", TARGET_LUX);
    return ESP_OK;
}