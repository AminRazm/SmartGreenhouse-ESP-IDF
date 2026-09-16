#include "fan.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <inttypes.h>

/* ---------- پیکربندی PWM ---------- */
#define FAN_PWM_GPIO        GPIO_NUM_5
#define FAN_PWM_FREQUENCY   25000
#define FAN_PWM_RESOLUTION  LEDC_TIMER_10_BIT
#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_SPEED_MODE  LEDC_LOW_SPEED_MODE

/* ---------- duty خام هر سرعت (10 بیتی) ---------- */
#define FAN_DUTY_OFF    0
#define FAN_DUTY_LOW    400   /* معادل «کند»؛ اندکی رو به پایین گرد شد */
#define FAN_DUTY_MEDIUM 700   /* معادل «متوسط» */
#define FAN_DUTY_HIGH   1023  /* تمام‌قد؛ بالاترین duty مجاز برای 10 بیت */

static const char *TAG = "fan";

esp_err_t fan_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = FAN_PWM_SPEED_MODE,
        .timer_num = FAN_PWM_TIMER,
        .duty_resolution = FAN_PWM_RESOLUTION,
        .freq_hz = FAN_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = FAN_PWM_GPIO,
        .speed_mode = FAN_PWM_SPEED_MODE,
        .channel = FAN_PWM_CHANNEL,
        .timer_sel = FAN_PWM_TIMER,
        .duty = FAN_DUTY_OFF,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Fan control initialized, starting OFF");
    return ESP_OK;
}

esp_err_t fan_control_set_speed(fan_speed_t speed)
{
    /* جدول ثابت duty؛ بدون هیچ تبدیل و مپینگ درصدی */
    static const uint32_t duties[] = {
        [FAN_SPEED_OFF] = FAN_DUTY_OFF,
        [FAN_SPEED_LOW] = FAN_DUTY_LOW,
        [FAN_SPEED_MEDIUM] = FAN_DUTY_MEDIUM,
        [FAN_SPEED_HIGH] = FAN_DUTY_HIGH,
    };

    esp_err_t err = ledc_set_duty(FAN_PWM_SPEED_MODE, FAN_PWM_CHANNEL, duties[speed]);
    if (err != ESP_OK) {
        return err;
    }

    err = ledc_update_duty(FAN_PWM_SPEED_MODE, FAN_PWM_CHANNEL);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Fan speed set to %d (duty=%" PRIu32 ")", speed, duties[speed]);
    return ESP_OK;
}
