#include "irrigation_manager.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "led.h"
#include "moisture.h"
#include "pump.h"
#include "shared_adc.h"

/* ---------- آستانه‌ها ---------- */

/* میانگین سنسور خاک بالای این عدد = خاک خشک، شروع آبیاری */
#define SOIL_DRY_THRESHOLD 1300

/* نمونه‌ی تازه‌ی سنسور خاک بالای این عدد = پراب بیرون از خاک، آبیاری ممنوع */
#define SOIL_OUT_OF_SOIL_THRESHOLD 2400

/* پراب مخزن بالای این عدد = مخزن خالی (بیرون آب = مدار باز = 4095) */
#define TANK_EMPTY_THRESHOLD 4000

/* ---------- زمان‌بندی‌ها ---------- */

/* استراحت بعد از هر چرخه (موفق یا ناموفق): آب فرصت نفوذ پیدا می‌کند و
 * در صورت خالی بودن مخزن، هر 5 دقیقه یک‌بار دوباره چک می‌شود. */
#define IRRIGATION_COOLDOWN_MS (5 * 60 * 1000)

#define TANK_SETTLE_MS      2000 /* فاصله‌ی روشن‌کردن تا خواندن پراب مخزن */
#define TANK_POLL_MS        1000 /* فاصله‌ی پایش مخزن در طول پمپاژ */
#define TANK_FINAL_DELAY_MS 200  /* مکث کوتاه قبل از خواندن نهایی مخزن */

/* ---------- سخت‌افزار ---------- */

#define WATER_LEVEL_POWER_GPIO GPIO_NUM_18 /* تغذیه‌ی پراب مخزن */

#define WATER_LEVEL_ADC_CHANNEL     ADC_CHANNEL_2 /* GPIO3 روی ESP32-S3 */
#define WATER_LEVEL_ADC_BITWIDTH    ADC_BITWIDTH_12
#define WATER_LEVEL_ADC_ATTENUATION ADC_ATTEN_DB_12

/* median چند نمونه برای حذف نویز خواندن مخزن */
#define TANK_SAMPLE_COUNT       5
#define TANK_SAMPLE_INTERVAL_MS 10

/* در صورت اشغال ADC توسط سنسور دیگر (ESP_ERR_TIMEOUT) تلاش مجدد */
#define TANK_READ_RETRIES   3
#define TANK_RETRY_DELAY_MS 20

/* رویداد «خاک خشک شد» بین callback سنسور و تسک آبیاری */
#define EV_SOIL_DRY BIT0

static const char *TAG = "irrigation_manager";

static adc_oneshot_unit_handle_t s_tank_adc_handle;
static EventGroupHandle_t s_events;

/* تا این لحظه + cooldown به سنسور خاک توجه نمی‌شود */
static int64_t s_next_allowed_us;

/* چرخه‌ی آبیاری در جریان است؟ (GPIO18 و پمپ فقط توسط تسک کنترل می‌شوند) */
static volatile bool s_cycle_active;

/* آخرین وضعیت مخزن برای داشبورد */
static volatile water_tank_status_t s_tank_status = WATER_TANK_STATUS_UNKNOWN;

/* برای لاگ‌های تکراری که فقط یک‌بار لازم است */
static bool s_logged_out_of_soil;
static bool s_logged_cooldown_skip;


static int64_t now_us(void)
{
    return esp_timer_get_time();
}


/**
 * @brief خواندن مخزن: median چند نمونه
 *
 * اگر ADC هم‌زمان توسط سنسور دیگری مشغول باشد ESP_ERR_TIMEOUT می‌آید؛
 * فقط برای همین خطا چند بار تلاش می‌کنیم.
 */
static esp_err_t tank_read_median(int *out_raw)
{
    int samples[TANK_SAMPLE_COUNT];
    esp_err_t err = ESP_OK;

    for (int i = 0; i < TANK_SAMPLE_COUNT; ++i) {

        for (int attempt = 0;; ++attempt) {

            err = adc_oneshot_read(s_tank_adc_handle, WATER_LEVEL_ADC_CHANNEL, &samples[i]);

            if (err != ESP_ERR_TIMEOUT || attempt + 1 >= TANK_READ_RETRIES) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(TANK_RETRY_DELAY_MS));
        }

        if (err != ESP_OK) {
            return err;
        }

        if (i + 1 < TANK_SAMPLE_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(TANK_SAMPLE_INTERVAL_MS));
        }
    }

    /* insertion sort برای گرفتن median */
    for (int i = 1; i < TANK_SAMPLE_COUNT; ++i) {

        const int key = samples[i];
        int j = i - 1;

        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = key;
    }

    *out_raw = samples[TANK_SAMPLE_COUNT / 2];
    return ESP_OK;
}


/**
 * @brief پایان هر چرخه در همه‌ی حالت‌ها: پمپ و پراب قطع، LED وضعیت مخزن، cooldown
 */
static void end_cycle(void)
{
    pump_stop();                            /* امانی: پمپ در هیچ حالتی روشن نمی‌ماند */
    gpio_set_level(WATER_LEVEL_POWER_GPIO, 0); /* پراب بی‌برق تا چرخه‌ی بعد */

    /* ال ای دی: فقط در صورت خالی بودن مخزن، چشمک آبی کم‌نور می‌ماند */
    led_set_pattern(s_tank_status == WATER_TANK_STATUS_EMPTY
                        ? LED_PATTERN_TANK_EMPTY
                        : LED_PATTERN_OFF);

    s_next_allowed_us = now_us() + (int64_t)IRRIGATION_COOLDOWN_MS * 1000;
    s_cycle_active = false;

    ESP_LOGI(TAG, "irrigation: cycle finished, GPIO18=OFF, next check in %d min",
             IRRIGATION_COOLDOWN_MS / 60000);
}


/**
 * @brief یک چرخه‌ی کامل آبیاری (فقط داخل irrigation_task اجرا می‌شود)
 *
 * پراب روشن → چک مخزن → پمپ 10 ثانیه با پایش مخزن → خواندن نهایی → قطع همه.
 */
static void run_irrigation_cycle(void)
{
    s_cycle_active = true;

    /* ---------- فاز 1: چک مخزن قبل از پمپ ---------- */

    gpio_set_level(WATER_LEVEL_POWER_GPIO, 1);

    /* تا اولین خواندن معتبر وضعیت مخزن نامشخص است؛ الگوی هشدار موقت */
    led_set_pattern(LED_PATTERN_TANK_EMPTY);

    ESP_LOGI(TAG, "irrigation: soil dry, tank probe ON (GPIO18), settling %d ms", TANK_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(TANK_SETTLE_MS));

    int raw = 0;
    esp_err_t err = tank_read_median(&raw);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "irrigation: tank read failed (%s), cycle cancelled", esp_err_to_name(err));
        end_cycle();
        return;
    }

    ESP_LOGI(TAG, "irrigation: pre-pump raw=%d empty_threshold=%d -> %s",
             raw, TANK_EMPTY_THRESHOLD,
             raw > TANK_EMPTY_THRESHOLD ? "TANK EMPTY" : "water present");

    if (raw > TANK_EMPTY_THRESHOLD) {

        s_tank_status = WATER_TANK_STATUS_EMPTY;
        ESP_LOGW(TAG, "irrigation: water tank is EMPTY (raw=%d), pump not started", raw);

        end_cycle(); /* ال ای دی چشمک آبی می‌ماند تا مخزن پر شود */
        return;
    }

    s_tank_status = WATER_TANK_STATUS_HAS_WATER;
    led_set_pattern(LED_PATTERN_OFF);

    /* ---------- فاز 2: پمپ 10 ثانیه‌ای با پایش مخزن ---------- */

    err = pump_start_pulse();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "irrigation: pump start failed (%s)", esp_err_to_name(err));
        end_cycle();
        return;
    }

    led_set_pattern(LED_PATTERN_PUMP_ACTIVE); /* چشمک سریع آبی تا پایان پمپ */

    while (pump_is_running()) {

        vTaskDelay(pdMS_TO_TICKS(TANK_POLL_MS));

        if (!pump_is_running()) {
            break; /* پالس 10 ثانیه‌ای پمپ طبیعی تمام شد */
        }

        err = tank_read_median(&raw);

        if (err == ESP_OK && raw > TANK_EMPTY_THRESHOLD) {

            pump_stop();
            s_tank_status = WATER_TANK_STATUS_EMPTY;

            ESP_LOGE(TAG, "irrigation: tank ran DRY mid-pump (raw=%d), pump stopped immediately", raw);
            break;
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "irrigation: mid-pump tank read failed (%s)", esp_err_to_name(err));
        }
    }

    /* ---------- فاز 3: خواندن نهایی مخزن (پراب هنوز برق دارد) ---------- */

    vTaskDelay(pdMS_TO_TICKS(TANK_FINAL_DELAY_MS));

    err = tank_read_median(&raw);
    if (err == ESP_OK) {

        const bool empty = raw > TANK_EMPTY_THRESHOLD;
        s_tank_status = empty ? WATER_TANK_STATUS_EMPTY : WATER_TANK_STATUS_HAS_WATER;

        ESP_LOGI(TAG, "irrigation: final tank state raw=%d -> %s",
                 raw, empty ? "EMPTY" : "HAS WATER");

    } else {
        ESP_LOGW(TAG, "irrigation: final tank read failed (%s), keeping previous status",
                 esp_err_to_name(err));
    }

    end_cycle();
}


/**
 * @brief تسک آبیاری: تا رویداد «خاک خشک» صبر می‌کند، بعد یک چرخه را اجرا می‌کند
 */
static void irrigation_task(void *arg)
{
    (void)arg;

    for (;;) {

        /* رویداد در همان لحظه‌ی خواندن مصرف می‌شود */
        (void)xEventGroupWaitBits(s_events, EV_SOIL_DRY, pdTRUE, pdFALSE, portMAX_DELAY);

        run_irrigation_cycle();
    }
}


/**
 * @brief callback سنسور رطوبت خاک (هر 1 ثانیه، از تسک moisture)
 *
 * ترتیب چک‌ها: اول اعتبار سنسور (بیرون خاک)، بعد خشکی، بعد cooldown.
 */
static void on_moisture_reading(const moisture_reading_t *reading, void *context)
{
    (void)context;

    ESP_LOGI(TAG, "Soil raw=%d", reading->raw_value);

    /* پراب بیرون از خاک؟ با نمونه‌ی تازه چک می‌شود، نه میانگین کند */
    if (reading->latest_sample >= SOIL_OUT_OF_SOIL_THRESHOLD) {

        if (!s_logged_out_of_soil) {
            ESP_LOGW(TAG, "Soil probe looks OUT OF SOIL (raw=%d), readings ignored",
                     reading->latest_sample);
            s_logged_out_of_soil = true;
        }
        return;
    }
    s_logged_out_of_soil = false;

    if (reading->raw_value < SOIL_DRY_THRESHOLD || s_cycle_active) {
        return; /* خاک مرطوب است یا چرخه در جریان است */
    }

    if (now_us() < s_next_allowed_us) {

        if (!s_logged_cooldown_skip) {

            const int64_t remaining_s =
                (s_next_allowed_us - now_us() + 999999) / 1000000;

            ESP_LOGI(TAG, "Soil dry (raw=%d) but cooling down, next check in %lld s",
                     reading->raw_value, (long long)remaining_s);

            s_logged_cooldown_skip = true;
        }
        return;
    }
    s_logged_cooldown_skip = false;

    /* همه‌ی شرط‌ها OK: رویداد شروع چرخه را بفرست */
    xEventGroupSetBits(s_events, EV_SOIL_DRY);
}


water_tank_status_t irrigation_manager_get_tank_status(void)
{
    return s_tank_status;
}


esp_err_t irrigation_manager_init(void)
{
    esp_err_t err = pump_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pump_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = shared_adc1_get_handle(&s_tank_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "shared_adc1_get_handle failed: %s", esp_err_to_name(err));
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = WATER_LEVEL_ADC_ATTENUATION,
        .bitwidth = WATER_LEVEL_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_tank_adc_handle, WATER_LEVEL_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    /* GPIO18 فقط تغذیه‌ی پراب مخزن است؛ خارج از چرخه‌ی آبیاری کاملاً خاموش */
    const gpio_config_t power_config = {
        .pin_bit_mask = 1ULL << WATER_LEVEL_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&power_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(GPIO18) failed: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(WATER_LEVEL_POWER_GPIO, 0);

    /* ال ای دی اینجا راه‌اندازی می‌شود چون تنها مصرف‌کننده‌اش همین جاست */
    err = led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = moisture_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "moisture_init failed: %s", esp_err_to_name(err));
        return err;
    }
    moisture_set_reading_callback(on_moisture_reading, NULL);

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreate(irrigation_task, "irrigation", 4096, NULL, 4, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Irrigation initialized: soil_dry=%d, out_of_soil=%d, tank_empty=%d, cooldown=%d min",
             SOIL_DRY_THRESHOLD, SOIL_OUT_OF_SOIL_THRESHOLD,
             TANK_EMPTY_THRESHOLD, IRRIGATION_COOLDOWN_MS / 60000);

    return ESP_OK;
}
