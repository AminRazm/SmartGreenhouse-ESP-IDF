#include "yl69_sensor.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_adc.h"

#define YL69_POWER_GPIO GPIO_NUM_18
#define YL69_ADC_CHANNEL ADC_CHANNEL_2
#define YL69_ADC_ATTENUATION ADC_ATTEN_DB_12
#define YL69_ADC_BITWIDTH ADC_BITWIDTH_12
#define YL69_SAMPLE_COUNT 5
#define YL69_SAMPLE_INTERVAL_MS 10
#define YL69_READ_RETRIES 3
#define YL69_RETRY_DELAY_MS 20

static adc_oneshot_unit_handle_t s_adc_handle;

static void sort_samples(int samples[YL69_SAMPLE_COUNT])
{
    for (int i = 1; i < YL69_SAMPLE_COUNT; ++i) {
        const int value = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > value) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = value;
    }
}

esp_err_t yl69_sensor_init(void)
{
    const gpio_config_t power_config = {
        .pin_bit_mask = 1ULL << YL69_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&power_config);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(YL69_POWER_GPIO, 0);

    err = shared_adc1_get_handle(&s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = YL69_ADC_ATTENUATION,
        .bitwidth = YL69_ADC_BITWIDTH,
    };
    return adc_oneshot_config_channel(s_adc_handle, YL69_ADC_CHANNEL, &channel_config);
}

esp_err_t yl69_sensor_set_power(bool enabled)
{
    return gpio_set_level(YL69_POWER_GPIO, enabled ? 1 : 0);
}

esp_err_t yl69_sensor_read_raw(int *raw_value)
{
    if (raw_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int samples[YL69_SAMPLE_COUNT];
    for (int i = 0; i < YL69_SAMPLE_COUNT; ++i) {
        esp_err_t err = ESP_FAIL;
        for (int attempt = 0; attempt < YL69_READ_RETRIES; ++attempt) {
            err = adc_oneshot_read(s_adc_handle, YL69_ADC_CHANNEL, &samples[i]);
            if (err != ESP_ERR_TIMEOUT) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(YL69_RETRY_DELAY_MS));
        }
        if (err != ESP_OK) {
            return err;
        }
        if (i + 1 < YL69_SAMPLE_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(YL69_SAMPLE_INTERVAL_MS));
        }
    }

    sort_samples(samples);
    *raw_value = samples[YL69_SAMPLE_COUNT / 2];
    return ESP_OK;
}
