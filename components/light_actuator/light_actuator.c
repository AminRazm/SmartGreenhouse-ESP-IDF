#include "light_actuator.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"


#define LIGHT_GPIO         GPIO_NUM_6
#define LIGHT_FREQUENCY_HZ 5000             
#define LIGHT_RESOLUTION   LEDC_TIMER_10_BIT 
#define LIGHT_TIMER        LEDC_TIMER_1      
#define LIGHT_CHANNEL      LEDC_CHANNEL_1    
#define LIGHT_SPEED_MODE   LEDC_LOW_SPEED_MODE


#define LIGHT_MAX_DUTY ((1U << LIGHT_RESOLUTION) - 1U)

static const char *TAG = "light_actuator";

esp_err_t light_actuator_init(void)
{

    gpio_config_t boot_pin = {
        .pin_bit_mask = 1ULL << LIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    esp_err_t err = gpio_config(&boot_pin);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(LIGHT_GPIO, 0);

    const ledc_timer_config_t timer_config = {
        .speed_mode = LIGHT_SPEED_MODE,
        .timer_num = LIGHT_TIMER,
        .duty_resolution = LIGHT_RESOLUTION,
        .freq_hz = LIGHT_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = LIGHT_GPIO,
        .speed_mode = LIGHT_SPEED_MODE,
        .channel = LIGHT_CHANNEL,
        .timer_sel = LIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Light initialized, starting OFF");
    return ESP_OK;
}

esp_err_t light_actuator_set_level(int level)
{
    if (level < 0) {
        level = 0;
    }
    if (level > (int)LIGHT_MAX_DUTY) {
        level = (int)LIGHT_MAX_DUTY;
    }

    esp_err_t err = ledc_set_duty(LIGHT_SPEED_MODE, LIGHT_CHANNEL, (uint32_t)level);
    if (err != ESP_OK) {
        return err;
    }

    return ledc_update_duty(LIGHT_SPEED_MODE, LIGHT_CHANNEL);
}
