#include "pump.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

/* ---------- تنظیمات قابل تغییر ---------- */
#define PUMP_GPIO           GPIO_NUM_4
#define PUMP_ON_DURATION_MS 10000  /* مدت‌زمان هر پالس آبیاری = ۱۰ ثانیه */

static const char *TAG = "pump";

static esp_timer_handle_t s_pulse_timer;
static volatile bool s_is_running;

/**
 * @brief callback ای که پس از پایان مدت پالس، پمپ را خاموش می‌کند
 */
static void on_pulse_timer_elapsed(void *arg)
{
    (void)arg;
    gpio_set_level(PUMP_GPIO, 0);
    s_is_running = false;
    ESP_LOGI(TAG, "Pump pulse finished, turned OFF");
}

esp_err_t pump_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PUMP_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(PUMP_GPIO, 0);

    const esp_timer_create_args_t timer_args = {
        .callback = on_pulse_timer_elapsed,
        .name = "pump_pulse",
    };
    return esp_timer_create(&timer_args, &s_pulse_timer);
}

esp_err_t pump_start_pulse(void)
{
    if (s_is_running) {
        ESP_LOGW(TAG, "Pump already running, ignoring start request");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_set_level(PUMP_GPIO, 1);
    s_is_running = true;

    esp_err_t err = esp_timer_start_once(s_pulse_timer, PUMP_ON_DURATION_MS * 1000ULL);
    if (err != ESP_OK) {
        gpio_set_level(PUMP_GPIO, 0);
        s_is_running = false;
        return err;
    }

    ESP_LOGI(TAG, "Pump pulse started (%d ms)", PUMP_ON_DURATION_MS);
    return ESP_OK;
}

void pump_stop(void)
{
    if (!s_is_running) {
        return;
    }

    /* تایمر پالس را هم لغو می‌کنیم تا بعداً دیر خاموش نکند */
    esp_timer_stop(s_pulse_timer);

    gpio_set_level(PUMP_GPIO, 0);
    s_is_running = false;

    ESP_LOGW(TAG, "Pump stopped manually (pump_stop)");
}

bool pump_is_running(void)
{
    return s_is_running;
}
