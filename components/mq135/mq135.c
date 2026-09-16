#include "mq135.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_adc.h"

/* ---------- تنظیمات قابل تغییر ----------
 * کانال روی هندل مشترک ADC1 (ساخته‌شده در کامپوننت shared_adc) */
#define MQ135_ADC_CHANNEL    ADC_CHANNEL_1
#define MQ135_ADC_BITWIDTH   ADC_BITWIDTH_12
#define MQ135_ADC_ATTENUATION ADC_ATTEN_DB_12

#define MQ135_SAMPLE_COUNT    20   /* تعداد نمونه در پنجره میانگین متحرک */
#define MQ135_SAMPLE_PERIOD_MS 100 /* فاصله بین نمونه‌ها؛ پنجره کامل = 2 ثانیه */

static const char *TAG = "mq135";

static adc_oneshot_unit_handle_t s_adc_handle;

/* بافر دایره‌ای برای میانگین متحرک؛ فقط داخل mq135_task خوانده/نوشته می‌شود،
 * پس نیازی به قفل (mutex/critical section) نیست. */
static int s_samples[MQ135_SAMPLE_COUNT];
static int s_sample_sum;
static uint8_t s_sample_index;
static uint8_t s_sample_count;

static mq135_reading_callback_t s_callback;
static void *s_callback_ctx;

/**
 * @brief یک نمونه جدید را به بافر میانگین متحرک اضافه کرده و میانگین جدید را برمی‌گرداند
 */
static int push_sample_and_get_average(int new_sample)
{
    if (s_sample_count == MQ135_SAMPLE_COUNT) {
        s_sample_sum -= s_samples[s_sample_index];
    } else {
        s_sample_count++;
    }

    s_samples[s_sample_index] = new_sample;
    s_sample_sum += new_sample;
    s_sample_index = (uint8_t)((s_sample_index + 1) % MQ135_SAMPLE_COUNT);

    return s_sample_sum / s_sample_count;
}

/**
 * @brief تسک پس‌زمینه‌ای که به‌صورت دوره‌ای ADC را می‌خواند و میانگین را گزارش می‌کند
 */
static void mq135_task(void *arg)
{
    (void)arg;

    while (true) {
        int raw_sample;
        const esp_err_t err = adc_oneshot_read(s_adc_handle, MQ135_ADC_CHANNEL, &raw_sample);
        if (err == ESP_OK) {
            const int average = push_sample_and_get_average(raw_sample);

            ESP_LOGI(TAG, "Gas raw=%d", average);

            if (s_callback != NULL) {
                const mq135_reading_t reading = { .raw_value = average };
                s_callback(&reading, s_callback_ctx);
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            /* ESP_ERR_TIMEOUT یعنی ADC هم‌زمان توسط سنسور دیگری مشغول است؛ این نمونه را رد می‌کنیم */
            ESP_LOGW(TAG, "ADC read failed");
        }

        vTaskDelay(pdMS_TO_TICKS(MQ135_SAMPLE_PERIOD_MS));
    }
}

esp_err_t mq135_init(void)
{
    esp_err_t err = shared_adc1_get_handle(&s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = MQ135_ADC_ATTENUATION,
        .bitwidth = MQ135_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc_handle, MQ135_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        return err;
    }

    const BaseType_t created = xTaskCreate(mq135_task, "mq135_task", 3072, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void mq135_set_reading_callback(mq135_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}