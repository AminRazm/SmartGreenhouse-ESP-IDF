#include "fan_actuator.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"


#define FAN_ACTUATOR_PWM_GPIO        GPIO_NUM_5
#define FAN_ACTUATOR_PWM_FREQUENCY   25000
#define FAN_ACTUATOR_PWM_RESOLUTION  LEDC_TIMER_10_BIT
#define FAN_ACTUATOR_PWM_TIMER       LEDC_TIMER_0
#define FAN_ACTUATOR_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_ACTUATOR_PWM_SPEED_MODE  LEDC_LOW_SPEED_MODE


#define FAN_ACTUATOR_DUTY_OFF    0
#define FAN_ACTUATOR_DUTY_LOW    400   
#define FAN_ACTUATOR_DUTY_MEDIUM 700   
#define FAN_ACTUATOR_DUTY_HIGH   1023  

static const char *TAG = "fan_actuator";

esp_err_t fan_actuator_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = FAN_ACTUATOR_PWM_SPEED_MODE,
        .timer_num = FAN_ACTUATOR_PWM_TIMER,
        .duty_resolution = FAN_ACTUATOR_PWM_RESOLUTION,
        .freq_hz = FAN_ACTUATOR_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = FAN_ACTUATOR_PWM_GPIO,
        .speed_mode = FAN_ACTUATOR_PWM_SPEED_MODE,
        .channel = FAN_ACTUATOR_PWM_CHANNEL,
        .timer_sel = FAN_ACTUATOR_PWM_TIMER,
        .duty = FAN_ACTUATOR_DUTY_OFF,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Fan control initialized, starting OFF");
    return ESP_OK;
}

esp_err_t fan_actuator_set_speed(fan_actuator_speed_t speed)
{

    static const uint32_t duties[] = {
        [FAN_ACTUATOR_SPEED_OFF] = FAN_ACTUATOR_DUTY_OFF,
        [FAN_ACTUATOR_SPEED_LOW] = FAN_ACTUATOR_DUTY_LOW,
        [FAN_ACTUATOR_SPEED_MEDIUM] = FAN_ACTUATOR_DUTY_MEDIUM,
        [FAN_ACTUATOR_SPEED_HIGH] = FAN_ACTUATOR_DUTY_HIGH,
    };

    esp_err_t err = ledc_set_duty(FAN_ACTUATOR_PWM_SPEED_MODE, FAN_ACTUATOR_PWM_CHANNEL, duties[speed]);
    if (err != ESP_OK) {
        return err;
    }

    err = ledc_update_duty(FAN_ACTUATOR_PWM_SPEED_MODE, FAN_ACTUATOR_PWM_CHANNEL);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}
