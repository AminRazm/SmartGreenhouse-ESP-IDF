#include "dht22.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------- تنظیمات ---------- */
#define DHT22_GPIO                 GPIO_NUM_7
#define DHT22_READ_INTERVAL_MS     3000
#define DHT22_POWER_UP_DELAY_MS    2000

#define DHT22_START_SIGNAL_LOW_MS  10
#define DHT22_START_SIGNAL_HIGH_US 40

/* تایم‌اوت‌ها با حاشیه‌ی امن؛ سنسور ضعیف گاهی پاسخ را دیر می‌دهد */
#define DHT22_ACK_TIMEOUT_US       5000
#define DHT22_PHASE_TIMEOUT_US     500
#define DHT22_BIT_ONE_THRESHOLD_US 50

#define DHT22_MAX_RETRIES          3
#define DHT22_RETRY_GAP_MS         100

/* اگر این‌قدر پشت‌سرهم خواندن کامل خط بخورد، یک هشدار داده می‌شود
 * (خطاهای گذرا زیرِ این آستانه بی‌صدا رد می‌شوند تا لاگ شلوغ نشود) */
#define DHT22_CONSECUTIVE_FAIL_WARN 5

static const char *TAG = "dht22";

static dht22_reading_callback_t s_callback;
static void *s_callback_ctx;

/**
 * @brief تا رسیدن GPIO به سطح موردنظر صبر می‌کند؛ در حین انتظار CPU آزاد است
 */
static esp_err_t wait_for_level(int level, uint32_t timeout_us)
{
    const int64_t deadline = esp_timer_get_time() + timeout_us;

    while (gpio_get_level(DHT22_GPIO) != level) {
        if (esp_timer_get_time() >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        taskYIELD(); /* تسک هم‌زمان CPU را قفل نکند */
    }
    return ESP_OK;
}

/**
 * @brief ارسال سیگنال شروع و انتظار برای پاسخ DHT22
 */
static esp_err_t send_start_signal_and_wait_response(void)
{
    /* MCU: LOW ده میلی‌ثانیه، سپس HIGH چهل میکروثانیه، سپس آزادسازی خط.
     * پاسخ DHT22: LOW ~80us، HIGH ~80us، LOW ~80us. */
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(DHT22_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT22_START_SIGNAL_LOW_MS));
    gpio_set_level(DHT22_GPIO, 1);
    esp_rom_delay_us(DHT22_START_SIGNAL_HIGH_US);

    /* حالا DHT22 کنترل خط را در دست می‌گیرد */
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_INPUT);

    /* اولین LOW پاسخ ممکن است تا چند صد میکروثانیه بعد بیاید */
    if (wait_for_level(0, DHT22_ACK_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(1, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(0, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/**
 * @brief خواندن یک بیت: HIGH کوتاه = 0، HIGH بلند = 1
 */
static esp_err_t read_bit(bool *bit_value)
{
    /* LOW ~50us */
    if (wait_for_level(1, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    const int64_t high_start = esp_timer_get_time();

    /* پایان HIGH */
    if (wait_for_level(0, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    *bit_value = (esp_timer_get_time() - high_start) > DHT22_BIT_ONE_THRESHOLD_US;
    return ESP_OK;
}

/**
 * @brief خواندن 40 بیتِ پاسخ
 */
static esp_err_t read_raw_data(uint8_t data[5])
{
    for (int byte_idx = 0; byte_idx < 5; ++byte_idx) {
        data[byte_idx] = 0;
        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
            bool bit_value = false;
            if (read_bit(&bit_value) != ESP_OK) {
                return ESP_ERR_TIMEOUT;
            }
            data[byte_idx] = (uint8_t)((data[byte_idx] << 1) | (bit_value ? 1 : 0));
        }
    }
    return ESP_OK;
}

/**
 * @brief بررسی جمع کنترلی
 */
static bool checksum_is_valid(const uint8_t data[5])
{
    return (uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4];
}

/**
 * @brief تبدیل داده‌ی خام به دما/رطوبت
 */
static void decode_reading(const uint8_t data[5], dht22_reading_t *reading)
{
    const uint16_t humidity_raw = ((uint16_t)data[0] << 8) | data[1];
    uint16_t temperature_raw = ((uint16_t)data[2] << 8) | data[3];

    const bool is_negative = (temperature_raw & 0x8000U) != 0;
    temperature_raw &= 0x7FFFU;

    reading->humidity = humidity_raw / 10.0f;
    reading->temperature = temperature_raw / 10.0f;
    if (is_negative) {
        reading->temperature = -reading->temperature;
    }
}

/**
 * @brief یک خواندن کامل با چند تلاش؛ خطاهای گذرا را خودش هضم می‌کند
 */
static esp_err_t dht22_read(dht22_reading_t *reading)
{
    for (int attempt = 0; attempt < DHT22_MAX_RETRIES; ++attempt) {
        esp_err_t err = send_start_signal_and_wait_response();
        if (err == ESP_OK) {
            uint8_t data[5] = {0};
            err = read_raw_data(data);
            if (err == ESP_OK && !checksum_is_valid(data)) {
                err = ESP_ERR_INVALID_CRC;
            }
            if (err == ESP_OK) {
                decode_reading(data, reading);
                return ESP_OK;
            }
        }

        if (attempt + 1 < DHT22_MAX_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(DHT22_RETRY_GAP_MS));
        }
    }
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief تسک پس‌زمینه: هر 3 ثانیه یک خواندن
 */
static void dht22_task(void *arg)
{
    (void)arg;

    /* DHT22 بعد از تغذیه به 1-2 ثانیه زمان برای آماده‌شدن نیاز دارد */
    vTaskDelay(pdMS_TO_TICKS(DHT22_POWER_UP_DELAY_MS));
    ESP_LOGI(TAG, "DHT22 power-up delay done, starting reads");

    int consecutive_failures = 0;

    while (true) {
        dht22_reading_t reading;
        const esp_err_t err = dht22_read(&reading);

        if (err == ESP_OK) {
            consecutive_failures = 0;

            ESP_LOGI(TAG, "T=%.1f C, RH=%.1f %%", reading.temperature, reading.humidity);

            if (s_callback != NULL) {
                s_callback(&reading, s_callback_ctx);
            }
        } else {
            /* خطاهای تکی/گذرا بی‌صدا؛ فقط خطای پیوسته هشدار می‌دهد */
            consecutive_failures++;
            if (consecutive_failures == DHT22_CONSECUTIVE_FAIL_WARN) {
                ESP_LOGW(TAG, "DHT22 read failed %d times in a row (sensor unplugged?)", consecutive_failures);
            } else if (consecutive_failures > DHT22_CONSECUTIVE_FAIL_WARN &&
                       consecutive_failures % DHT22_CONSECUTIVE_FAIL_WARN == 0) {
                ESP_LOGW(TAG, "DHT22 still failing (%d in a row)", consecutive_failures);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DHT22_READ_INTERVAL_MS));
    }
}

esp_err_t dht22_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << DHT22_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    /* آزاد کردن خط */
    gpio_set_level(DHT22_GPIO, 1);

    const BaseType_t created = xTaskCreate(dht22_task, "dht22_task", 4096, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void dht22_set_reading_callback(dht22_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}
