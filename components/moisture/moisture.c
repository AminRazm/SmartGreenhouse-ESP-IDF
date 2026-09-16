#include "moisture.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_adc.h"

/* ---------- تنظیمات قابل تغییر ---------- */

/* کانال روی هندل مشترک ADC1 (GPIO1 روی YD-ESP32-S3) */
#define MOISTURE_ADC_CHANNEL     ADC_CHANNEL_0
#define MOISTURE_ADC_BITWIDTH    ADC_BITWIDTH_12
#define MOISTURE_ADC_ATTENUATION ADC_ATTEN_DB_12

/* فاصله هر نمونه‌گیری ADC */
#define MOISTURE_SAMPLE_PERIOD_MS 1000

/* میانگین روی این تعداد نمونه‌ی سالم */
#define MOISTURE_AVERAGE_COUNT    10

/* بیشتر از این پرش نسبت به نمونه‌ی قبلی = داده مشکوک (نویز / پراب شناور) */
#define MOISTURE_MAX_STEP         60

/* یک پرش بزرگ فقط بعد از این تعداد نمونه‌ی پشت‌سرهم تأیید می‌شود
 * (ریختن آب واقعی چند نمونه‌ی پشت‌سرهم می‌سازد؛ نویز تصادفی نمی‌سازد) */
#define MOISTURE_JUMP_CONFIRMS    5

static const char *TAG = "moisture";

static adc_oneshot_unit_handle_t s_adc_handle;

static int s_samples[MOISTURE_AVERAGE_COUNT];
static int s_sample_sum;
static uint8_t s_sample_index;
static uint8_t s_sample_count;

static moisture_reading_callback_t s_callback;
static void *s_callback_ctx;

/* آخرین نمونه‌ی خام سالم + شمارنده‌ی تأیید پرش بزرگ */
static int s_last_good_sample = -1;
static int s_jump_candidates;

static int push_sample_and_get_average(int new_sample)
{
    if (s_sample_count == MOISTURE_AVERAGE_COUNT) {
        s_sample_sum -= s_samples[s_sample_index];
    } else {
        s_sample_count++;
    }

    s_samples[s_sample_index] = new_sample;
    s_sample_sum += new_sample;
    s_sample_index = (uint8_t)((s_sample_index + 1) % MOISTURE_AVERAGE_COUNT);

    return s_sample_sum / s_sample_count;
}

/**
 * @brief فیلتر داده‌ی پرت: فقط پرش‌های کوچک فوری پذیرفته می‌شوند
 *
 * پرش بزرگ (سنسور از خاک درآمدن، آب ریختن، سنسور شناور) باید
 * MOISTURE_JUMP_CONFIRMS بار پشت‌سرهم دیده شود تا واقعی حساب شود.
 */
static bool sample_is_plausible(int new_sample)
{
    if (s_last_good_sample < 0) {
        return true; /* اولین نمونه‌ی بوت همیشه قبول است */
    }

    int step = new_sample - s_last_good_sample;

    if (step < 0) {
        step = -step;
    }

    if (step <= MOISTURE_MAX_STEP) {
        s_jump_candidates = 0;
        return true;
    }

    /* پرش بزرگ: تا وقتی پشت‌سرهم تکرار نشده، رد */
    if (++s_jump_candidates < MOISTURE_JUMP_CONFIRMS) {
        return false;
    }

    /* MOISTURE_JUMP_CONFIRMS نمونه‌ی پیوسته در ناحیه‌ی جدید = تغییر واقعی */
    ESP_LOGW(TAG, "Big jump accepted after %d confirms: %d -> %d",
             MOISTURE_JUMP_CONFIRMS, s_last_good_sample, new_sample);
    s_jump_candidates = 0;
    return true;
}

static void moisture_task(void *arg)
{
    (void)arg;

    while (true) {

        int raw_sample;
        const esp_err_t err = adc_oneshot_read(s_adc_handle, MOISTURE_ADC_CHANNEL, &raw_sample);

        if (err == ESP_OK && sample_is_plausible(raw_sample)) {

            s_last_good_sample = raw_sample;
            const int average = push_sample_and_get_average(raw_sample);

            if (s_callback != NULL) {
                const moisture_reading_t reading = {
                    .raw_value = average,
                    .latest_sample = raw_sample,
                };
                s_callback(&reading, s_callback_ctx);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MOISTURE_SAMPLE_PERIOD_MS));
    }
}

esp_err_t moisture_init(void)
{
    esp_err_t err = shared_adc1_get_handle(&s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = MOISTURE_ADC_ATTENUATION,
        .bitwidth = MOISTURE_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc_handle, MOISTURE_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        return err;
    }

    const BaseType_t created = xTaskCreate(moisture_task, "moisture_task", 3072, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void moisture_set_reading_callback(moisture_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}
