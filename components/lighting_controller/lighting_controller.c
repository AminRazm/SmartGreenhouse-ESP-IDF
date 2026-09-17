#include "lighting_controller.h"

#include "bh1750_sensor.h"
#include "esp_log.h"
#include "light_actuator.h"
#include <stdbool.h>
#include <time.h>


#define TARGET_LUX        3500.0f 
#define ACTIVE_HOUR_START 6      
#define ACTIVE_HOUR_END   21     


#define LUX_DEADBAND_RATIO 0.05f 


#define LUX_EMA_ALPHA 0.5f


#define STEP_DIVISOR 10.0f
#define STEP_MIN     1
#define STEP_MAX     204


#define LIGHT_LEVEL_MAX 1023


static int adaptive_step(float lux_smooth)
{
    const float error = lux_smooth > TARGET_LUX ? lux_smooth - TARGET_LUX
                                                : TARGET_LUX - lux_smooth;
    int step = (int)(error / STEP_DIVISOR);
    if (step < STEP_MIN) {
        step = STEP_MIN;
    }
    if (step > STEP_MAX) {
        step = STEP_MAX;
    }
    return step;
}


#define STEADY_LOG_EVERY_TICKS 30


#define MIN_VALID_EPOCH 1600000000

static const char *TAG = "lighting_controller";


typedef struct {
    int level;              
    float lux_ema;          
    int steady_ticks;       
    bool has_time;          
    bool saw_first_reading; 
} light_actuator_ctrl_t;

static light_actuator_ctrl_t s_ctrl = {
    .level = 0,
    .lux_ema = -1.0f,
    .steady_ticks = 0,
    .has_time = false,
    .saw_first_reading = false,
};

static bool has_valid_time(void)
{
    return time(NULL) >= MIN_VALID_EPOCH;
}

static bool is_within_active_hours(void)
{
    const time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    return local_time.tm_hour >= ACTIVE_HOUR_START && local_time.tm_hour < ACTIVE_HOUR_END;
}


static void apply_level(int level, float lux)
{
    s_ctrl.level = level;
    s_ctrl.steady_ticks = 0;
    (void)light_actuator_set_level(level);
    ESP_LOGI(TAG, "Lux=%.0f target=%.0f -> level=%d", lux, TARGET_LUX, level);
}


static void control_tick(float lux)
{

    if (s_ctrl.lux_ema < 0.0f) {
        s_ctrl.lux_ema = lux;
    } else {
        s_ctrl.lux_ema += LUX_EMA_ALPHA * (lux - s_ctrl.lux_ema);
    }
    const float lux_smooth = s_ctrl.lux_ema;

    const bool has_time = has_valid_time();

    if (has_time != s_ctrl.has_time) {
        if (has_time) {
            ESP_LOGI(TAG, "Clock available: light active %02d:00-%02d:00 only",
                     ACTIVE_HOUR_START, ACTIVE_HOUR_END);
        }
        s_ctrl.has_time = has_time;
    }

    if (!has_time) {
        if (s_ctrl.level != 0) {
            s_ctrl.level = 0;
            (void)light_actuator_set_level(0);
        }
        return;
    }


    if (has_time && !is_within_active_hours()) {
        if (s_ctrl.level != 0) {
            s_ctrl.level = 0;
            (void)light_actuator_set_level(0);
            ESP_LOGI(TAG, "Outside active hours, light OFF");
        }
        return;
    }


    const float deadband = TARGET_LUX * LUX_DEADBAND_RATIO;
    if (lux_smooth > TARGET_LUX - deadband && lux_smooth < TARGET_LUX + deadband) {


        if (++s_ctrl.steady_ticks >= STEADY_LOG_EVERY_TICKS) {
            s_ctrl.steady_ticks = 0;
            ESP_LOGI(TAG, "steady: Lux=%.0f target=%.0f -> level=%d",
                     lux_smooth, TARGET_LUX, s_ctrl.level);
        }
        return;
    }


    const int step = adaptive_step(lux_smooth);
    int new_level;
    if (lux_smooth < TARGET_LUX) {
        new_level = s_ctrl.level + step; 
        if (new_level > LIGHT_LEVEL_MAX) {
            new_level = LIGHT_LEVEL_MAX;
        }
    } else {
        new_level = s_ctrl.level - step; 
        if (new_level < 0) {
            new_level = 0;
        }
    }
    apply_level(new_level, lux_smooth);
}


static void on_light_reading(const bh1750_sensor_reading_t *reading, void *context)
{
    light_actuator_ctrl_t *ctrl = context;

    if (!ctrl->saw_first_reading) {
        ctrl->saw_first_reading = true;
        ESP_LOGI(TAG, "First lux reading received, starting light control");
    }

    control_tick(reading->lux);
}

esp_err_t lighting_controller_init(void)
{
    esp_err_t err = light_actuator_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "light_actuator_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bh1750_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bh1750_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }
    bh1750_sensor_set_reading_callback(on_light_reading, &s_ctrl);

    ESP_LOGI(TAG, "Lighting controller initialized (target=%.0f lux)", TARGET_LUX);
    return ESP_OK;
}
