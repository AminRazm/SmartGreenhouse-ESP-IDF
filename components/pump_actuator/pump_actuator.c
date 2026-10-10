#include "pump_actuator.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define PUMP_GPIO GPIO_NUM_4
#define PUMP_ON_DURATION_MS 10000

static const char *TAG = "pump_actuator";
static esp_timer_handle_t s_pulse_timer;
static volatile bool s_is_running;

static void on_pulse_timer_elapsed(void *arg)
{
    (void)arg;
    gpio_set_level(PUMP_GPIO, 0);
    s_is_running = false;
}

esp_err_t pump_actuator_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PUMP_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK)
    {
        return err;
    }
    gpio_set_level(PUMP_GPIO, 0);
    const esp_timer_create_args_t timer_args = {
        .callback = on_pulse_timer_elapsed,
        .name = "pump_actuator_pulse",
    };
    return esp_timer_create(&timer_args, &s_pulse_timer);
}

esp_err_t pump_actuator_start_pulse(void)
{
    if (s_is_running)
    {
        ESP_LOGW(TAG, "Pump already running, ignoring start request");
        return ESP_ERR_INVALID_STATE;
    }
    gpio_set_level(PUMP_GPIO, 1);
    s_is_running = true;
    esp_err_t err = esp_timer_start_once(s_pulse_timer, PUMP_ON_DURATION_MS * 1000ULL);
    if (err != ESP_OK)
    {
        gpio_set_level(PUMP_GPIO, 0);
        s_is_running = false;
        return err;
    }
    return ESP_OK;
}

void pump_actuator_stop(void)
{
    if (!s_is_running)
    {
        return;
    }
    esp_timer_stop(s_pulse_timer);
    gpio_set_level(PUMP_GPIO, 0);
    s_is_running = false;
    ESP_LOGW(TAG, "Pump stopped manually (pump_actuator_stop)");
}

bool pump_actuator_is_running(void)
{
    return s_is_running;
}