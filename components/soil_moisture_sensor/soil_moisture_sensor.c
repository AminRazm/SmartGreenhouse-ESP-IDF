#include "soil_moisture_sensor.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_adc.h"

#define SOIL_MOISTURE_ADC_CHANNEL     ADC_CHANNEL_0
#define SOIL_MOISTURE_ADC_BITWIDTH    ADC_BITWIDTH_12
#define SOIL_MOISTURE_ADC_ATTENUATION ADC_ATTEN_DB_12

#define SOIL_MOISTURE_SAMPLE_PERIOD_MS 1000
#define SOIL_MOISTURE_AVERAGE_COUNT    10

static const char *TAG = "soil_moisture_sensor";

static adc_oneshot_unit_handle_t s_adc_handle;

static int s_samples[SOIL_MOISTURE_AVERAGE_COUNT];
static int s_sample_sum;
static uint8_t s_sample_index;
static uint8_t s_sample_count;

static soil_moisture_sensor_reading_callback_t s_callback;
static void *s_callback_ctx;

static int push_sample_and_get_average(int new_sample)
{
    if (s_sample_count == SOIL_MOISTURE_AVERAGE_COUNT) {
        s_sample_sum -= s_samples[s_sample_index];
    } else {
        s_sample_count++;
    }

    s_samples[s_sample_index] = new_sample;
    s_sample_sum += new_sample;
    s_sample_index = (uint8_t)((s_sample_index + 1) % SOIL_MOISTURE_AVERAGE_COUNT);

    return s_sample_sum / s_sample_count;
}

static void soil_moisture_sensor_task(void *arg)
{
    (void)arg;

    while (true) {
        int raw_sample;
        const esp_err_t err = adc_oneshot_read(s_adc_handle, SOIL_MOISTURE_ADC_CHANNEL, &raw_sample);

        if (err == ESP_OK) {
            const int average = push_sample_and_get_average(raw_sample);

            if (s_sample_count == SOIL_MOISTURE_AVERAGE_COUNT) {
                ESP_LOGI(TAG, "Soil moisture: average=%d", average);

                if (s_callback != NULL) {
                    const soil_moisture_sensor_reading_t reading = {
                        .average_value = average,
                    };
                    s_callback(&reading, s_callback_ctx);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SOIL_MOISTURE_SAMPLE_PERIOD_MS));
    }
}

esp_err_t soil_moisture_sensor_init(void)
{
    esp_err_t err = shared_adc1_get_handle(&s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = SOIL_MOISTURE_ADC_ATTENUATION,
        .bitwidth = SOIL_MOISTURE_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc_handle, SOIL_MOISTURE_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        return err;
    }

    const BaseType_t created = xTaskCreate(soil_moisture_sensor_task, "soil_moisture_sensor_task", 3072, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void soil_moisture_sensor_set_reading_callback(soil_moisture_sensor_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}
