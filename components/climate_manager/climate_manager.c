#include "climate_manager.h"

#include "dht22.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fan.h"
#include "mq135.h"
#include <math.h>

/* ---------- آستانه‌های روشن شدن (عبور رو به بالا) ---------- */
#define TEMPERATURE_ON_LOW    26.0f
#define TEMPERATURE_ON_MEDIUM 27.0f
#define TEMPERATURE_ON_FAST   28.0f

#define HUMIDITY_ON_LOW    65.0f
#define HUMIDITY_ON_MEDIUM 75.0f
#define HUMIDITY_ON_FAST   85.0f

#define GAS_ON_LOW    1800
#define GAS_ON_MEDIUM 2500
#define GAS_ON_FAST   3500

/* ---------- هیسترزیس ---------- */
/* کاهش پله فقط وقتی مجاز است که مقدار از «آستانه‌ی روشن منهای هیسترزیس»
 * هم پایین‌تر رفته باشد؛ در نتیجه نویز سنسور روی مرز، فن را مرتب زدن
 * نمی‌کند. عبور رو به بالا بدون تأخیر است. */
#define TEMPERATURE_HYSTERESIS 0.5f
#define HUMIDITY_HYSTERESIS    4.0f
#define GAS_HYSTERESIS         200

/* اگر از آخرین خواندن سالم DHT22 این‌قدر بگذرد، یک هشدار داده می‌شود؛
 * فن تا خواندن سالم بعدی روی سرعت فعلی می‌ماند. */
#define DHT22_STALE_WARN_MS 15000

static const char *TAG = "climate_manager";

/* ---------- وضعیت سنسورها ---------- */
static float s_temperature;
static float s_humidity;
static int   s_gas_raw;

/* فن قبل از اولین خواندن سالمِ هر سه سنسور تصمیم نمی‌گیرد */
static bool s_have_temperature;
static bool s_have_humidity;
static bool s_have_gas;

/* سطح فعلی هر معیار (0..3)؛ حافظه‌ی هیسترزیس همان این‌هاست */
static int s_level_temperature;
static int s_level_humidity;
static int s_level_gas;

/* زمان آخرین خواندن سالم DHT22 */
static int64_t s_dht22_last_ok_us;

/* آخرین سرعت اعمال‌شده روی فن؛ برای جلوگیری از آپدیت/لاگ تکراری.
 * fan_init هم فن را OFF راه‌اندازی می‌کند، پس مقدار اولیه سازگار است. */
static fan_speed_t s_last_applied_speed = FAN_SPEED_OFF;

/**
 * @brief بزرگ‌ترینِ دو سرعت؛ enum صعودی است پس مقایسه عددی کافی است
 */
static fan_speed_t higher_speed(fan_speed_t a, fan_speed_t b)
{
    return (a > b) ? a : b;
}

/**
 * @brief سطح جدید یک معیار از روی مقدار فعلی + سطحِ در اختیار (هیسترزیس)
 *
 * بالا رفتن پله: به محض رسیدن به آستانه‌ی روشن.
 * پایین آمدن پله: فقط وقتی مقدار از «آستانه‌ی پله‌ی فعلی منهای هیسترزیس»
 * هم پایین‌تر رفته باشد؛ وگرنه پله‌ی فعلی حفظ می‌شود.
 */
static int level_of(int current, const int on_threshold[3], int hysteresis, int held_level)
{
    int level = 0;
    for (int i = FAN_SPEED_LOW; i <= FAN_SPEED_HIGH; ++i) {
        if (current >= on_threshold[i - 1]) {
            level = i;
        }
    }

    if (level < held_level && current >= on_threshold[held_level - 1] - hysteresis) {
        level = held_level; /* هنوز داخل ناحیه‌ی هیسترزیس؛ پله حفظ می‌شود */
    }
    return level;
}

/**
 * @brief سرعت نهایی فن را از آخرین وضعیت همه معیارها می‌سازد و در صورت تغییر اعمال می‌کند
 */
static void evaluate_and_apply_fan_speed(void)
{
    /* فن تا اولین خواندن سالمِ هر سه سنسور دست نمی‌زند */
    if (!s_have_temperature || !s_have_humidity || !s_have_gas) {
        return;
    }

    /* اگر خواندن‌های DHT22 مدتیست خط می‌خورند فقط هشدار؛ تصمیم با
     * آخرین داده‌ی سالم ادامه می‌یابد تا خواندن سالم بعدی بیاید. */
    const int64_t now_us = esp_timer_get_time();
    if ((now_us - s_dht22_last_ok_us) / 1000 > DHT22_STALE_WARN_MS) {
        ESP_LOGW(TAG, "No valid DHT22 reading for %lld s; keeping current speed",
                 (long long)((now_us - s_dht22_last_ok_us) / 1000000));
        s_dht22_last_ok_us = now_us; /* این هشدار هر 15 ثانیه تکرار می‌شود */
    }

    /* سطح هر معیار از روی تازه‌ترین مقدارش + هیسترزیس */
    static const int temp_levels[3] = { (int)(TEMPERATURE_ON_LOW * 10), (int)(TEMPERATURE_ON_MEDIUM * 10), (int)(TEMPERATURE_ON_FAST * 10) };
    static const int hum_levels[3]  = { (int)HUMIDITY_ON_LOW, (int)HUMIDITY_ON_MEDIUM, (int)HUMIDITY_ON_FAST };
    static const int gas_levels[3]  = { GAS_ON_LOW, GAS_ON_MEDIUM, GAS_ON_FAST };

    s_level_temperature = level_of((int)(s_temperature * 10.0f), temp_levels, (int)(TEMPERATURE_HYSTERESIS * 10), s_level_temperature);
    s_level_humidity    = level_of((int)(s_humidity + 0.5f), hum_levels, (int)HUMIDITY_HYSTERESIS, s_level_humidity);
    s_level_gas         = level_of(s_gas_raw, gas_levels, GAS_HYSTERESIS, s_level_gas);

    /* نتیجه‌ی نهایی: بالاترینِ سه پیشنهاد */
    fan_speed_t speed = (fan_speed_t)higher_speed(higher_speed(s_level_temperature, s_level_humidity), s_level_gas);

    if (speed == s_last_applied_speed) {
        return; /* سرعت تغییری نکرده؛ بدون آپدیت و لاگ اضافه */
    }

    ESP_LOGI(TAG, "T=%.1f H=%.1f Gas=%d -> fan speed=%d", s_temperature, s_humidity, s_gas_raw, speed);

    if (fan_control_set_speed(speed) == ESP_OK) {
        s_last_applied_speed = speed;
    }
}

/**
 * @brief callback خواندن موفق DHT22؛ مقادیر غیرمنطقی رد می‌شوند
 */
static void on_dht22_reading(const dht22_reading_t *reading, void *context)
{
    (void)context;
    if (!isfinite(reading->temperature) || !isfinite(reading->humidity) ||
        reading->humidity < 0.0f || reading->humidity > 100.0f ||
        reading->temperature < -40.0f || reading->temperature > 80.0f) {
        ESP_LOGW(TAG, "DHT22 gave implausible values (T=%.1f H=%.1f), ignoring", reading->temperature, reading->humidity);
        return;
    }
    s_temperature = reading->temperature;
    s_humidity = reading->humidity;
    s_have_temperature = true;
    s_have_humidity = true;
    s_dht22_last_ok_us = esp_timer_get_time();
    evaluate_and_apply_fan_speed();
}

/**
 * @brief callback خواندن موفق MQ135
 */
static void on_mq135_reading(const mq135_reading_t *reading, void *context)
{
    (void)context;
    s_gas_raw = reading->raw_value;
    s_have_gas = true;
    evaluate_and_apply_fan_speed();
}

esp_err_t climate_manager_init(void)
{
    esp_err_t err = fan_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fan_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = dht22_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dht22_init failed: %s", esp_err_to_name(err));
        return err;
    }
    dht22_set_reading_callback(on_dht22_reading, NULL);

    err = mq135_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mq135_init failed: %s", esp_err_to_name(err));
        return err;
    }
    mq135_set_reading_callback(on_mq135_reading, NULL);

    ESP_LOGI(TAG, "Climate manager initialized");
    return ESP_OK;
}
