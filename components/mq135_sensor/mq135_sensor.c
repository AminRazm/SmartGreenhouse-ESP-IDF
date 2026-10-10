#include "mq135_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_adc.h"

#define MQ135_ADC_CHANNEL ADC_CHANNEL_1
#define MQ135_ADC_BITWIDTH ADC_BITWIDTH_12
#define MQ135_ADC_ATTENUATION ADC_ATTEN_DB_12
#define MQ135_SAMPLE_COUNT 20
#define MQ135_SAMPLE_PERIOD_MS 100
#define MQ135_LOG_INTERVAL_MS 5000

static const char *TAG = "mq135_sensor";
static adc_oneshot_unit_handle_t s_adc_handle;
static int s_samples[MQ135_SAMPLE_COUNT];
static int s_sample_sum;
static uint8_t s_sample_index;
static uint8_t s_sample_count;
static mq135_sensor_reading_callback_t s_callback;
static void *s_callback_ctx;
static int s_log_sample_count;

static int push_sample_and_get_average(int new_sample)
{
    if (s_sample_count == MQ135_SAMPLE_COUNT)
    {
        s_sample_sum -= s_samples[s_sample_index];
    }
    else
    {
        s_sample_count++;
    }

    s_samples[s_sample_index] = new_sample;
    s_sample_sum += new_sample;
    s_sample_index = (uint8_t)((s_sample_index + 1) % MQ135_SAMPLE_COUNT);

    return s_sample_sum / s_sample_count;
}

static void mq135_sensor_task(void *arg)
{
    (void)arg;

    while (true)
    {
        int raw_sample;
        const esp_err_t err = adc_oneshot_read(s_adc_handle, MQ135_ADC_CHANNEL, &raw_sample);
        if (err == ESP_OK)
        {
            const int average = push_sample_and_get_average(raw_sample);

            if (++s_log_sample_count >= MQ135_LOG_INTERVAL_MS / MQ135_SAMPLE_PERIOD_MS)
            {
                ESP_LOGI(TAG, "MQ135: raw=%d, average=%d", raw_sample, average);
                s_log_sample_count = 0;
            }

            if (s_callback != NULL)
            {
                const mq135_sensor_reading_t reading = {.raw_value = average};
                s_callback(&reading, s_callback_ctx);
            }
        }
        else if (err != ESP_ERR_TIMEOUT)
        {

            ESP_LOGW(TAG, "ADC read failed");
        }

        vTaskDelay(pdMS_TO_TICKS(MQ135_SAMPLE_PERIOD_MS));
    }
}

esp_err_t mq135_sensor_init(void)
{
    esp_err_t err = shared_adc1_get_handle(&s_adc_handle);
    if (err != ESP_OK)
    {
        return err;
    }
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = MQ135_ADC_ATTENUATION,
        .bitwidth = MQ135_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc_handle, MQ135_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK)
    {
        return err;
    }
    const BaseType_t created = xTaskCreate(mq135_sensor_task, "mq135_sensor_task", 3072, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void mq135_sensor_set_reading_callback(mq135_sensor_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}