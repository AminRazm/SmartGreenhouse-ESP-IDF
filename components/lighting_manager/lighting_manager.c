#include "lighting_manager.h"

#include "bh1750.h"
#include "esp_log.h"
#include "light.h"
#include <stdbool.h>
#include <time.h>

/* ---------- تنظیمات قابل تغییر ---------- */
#define TARGET_LUX        3500.0f /* لوکس هدف داخل گلخانه */
#define ACTIVE_HOUR_START 6      /* از 6:00 صبح */
#define ACTIVE_HOUR_END   21     /* تا 21:00؛ خارج این بازه LED خاموش */

/* اگر فاصله‌ی لوکس تا هدف کمتر از این نسبت بود، دست نزن (ضد نوسان) */
#define LUX_DEADBAND_RATIO 0.05f /* ±5% دور هدف */

/* ضریب میانگین نمایی لوکس؛ نویز لحظه‌ای سنسور را حذف می‌کند تا فقط
 * تغییرات واقعی و پایدار نور (مثل آفتاب یا چراغ قوه) کنترلر را تکان دهد */
#define LUX_EMA_ALPHA 0.5f

/* گام تغییر سطح PWM: یک‌دهم خطا، حداقل 1 و حداکثر 204 واحد؛
 * همیشه گام‌به‌گام — پرش کامل بین 0 و 1023 نوسان روشن/خاموش می‌سازد */
#define STEP_DIVISOR 10.0f
#define STEP_MIN     1
#define STEP_MAX     204

/* بیشینه‌ی سطح PWM (10 بیت) */
#define LIGHT_LEVEL_MAX 1023

/** گام متناسب با خطا: دور از هدف بزرگ، نزدیک هدف ریز (حداقل 1 واحد) */
static int adaptive_step(float lux_smooth)
{
    const float error = lux_smooth > TARGET_LUX ? lux_smooth - TARGET_LUX
                                                : TARGET_LUX - lux_smooth;
    int step = (int)(error / STEP_DIVISOR);
    if (step < STEP_MIN) {
        step = STEP_MIN;
    }
    if (step > STEP_MAX) {
        step = STEP_MAX;
    }
    return step;
}

/* وقتی نور پایدار است، هر این تعداد تیک (×2 ثانیه) یک لاگ وضعیت بده */
#define STEADY_LOG_EVERY_TICKS 30

/* کمتر از این مقدار epoch (حدوداً سال 2020) یعنی ساعت واقعی نداریم */
#define MIN_VALID_EPOCH 1600000000

static const char *TAG = "lighting_manager";

/* وضعیت کنترلر؛ فقط داخل callback سنسور دستکاری می‌شود */
typedef struct {
    int level;              /* سطح فعلی PWM */
    float lux_ema;          /* میانگین نمایی لوکس (-1 = هنوز مقداردهی نشده) */
    int steady_ticks;       /* شمار تیک‌های پایدار برای لاگ دوره‌ای */
    bool has_time;          /* وضعیت قبلی ساعت (برای لاگِ فقط هنگام تغییر) */
    bool saw_first_reading; /* اولین نمونه آمده؟ */
} light_ctrl_t;

static light_ctrl_t s_ctrl = {
    .level = 0,
    .lux_ema = -1.0f,
    .steady_ticks = 0,
    .has_time = false,
    .saw_first_reading = false,
};

static bool has_valid_time(void)
{
    return time(NULL) >= MIN_VALID_EPOCH;
}

static bool is_within_active_hours(void)
{
    const time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    return local_time.tm_hour >= ACTIVE_HOUR_START && local_time.tm_hour < ACTIVE_HOUR_END;
}

/**
 * @brief اعمال سطح جدید PWM + لاگ (با لوکس و هدف در همان خط)
 */
static void apply_level(int level, float lux)
{
    s_ctrl.level = level;
    s_ctrl.steady_ticks = 0;
    (void)light_set_level(level);
    ESP_LOGI(TAG, "Lux=%.0f target=%.0f -> level=%d", lux, TARGET_LUX, level);
}

/**
 * @brief هسته‌ی کنترل: با هر نمونه‌ی تازه‌ی سنسور یک گام به سمت هدف
 */
static void control_tick(float lux)
{
    /* میانگین نمایی لوکس: نمونه‌ی اول مستقیم می‌نشیند؛ نمونه‌های بعدی ترکیبی
     * از مقدار قبلی و تازه‌اند. نویز لحظه‌ای حذف و تغییر پایدار نور دیده می‌شود. */
    if (s_ctrl.lux_ema < 0.0f) {
        s_ctrl.lux_ema = lux;
    } else {
        s_ctrl.lux_ema += LUX_EMA_ALPHA * (lux - s_ctrl.lux_ema);
    }
    const float lux_smooth = s_ctrl.lux_ema;

    const bool has_time = has_valid_time();

    if (has_time != s_ctrl.has_time) {
        if (has_time) {
            ESP_LOGI(TAG, "Clock available: light active %02d:00-%02d:00 only",
                     ACTIVE_HOUR_START, ACTIVE_HOUR_END);
        } else {
            ESP_LOGW(TAG, "No clock yet: running 24h mode");
        }
        s_ctrl.has_time = has_time;
    }

    /* خارج از بازه‌ی ساعتی (فقط وقتی ساعت داریم) → خاموش */
    if (has_time && !is_within_active_hours()) {
        if (s_ctrl.level != 0) {
            s_ctrl.level = 0;
            (void)light_set_level(0);
            ESP_LOGI(TAG, "Outside active hours, light OFF");
        }
        return;
    }

    /* داخل نوار مرگی دور هدف → همان‌جا بمان؛ این همان ضد نوسان است */
    const float deadband = TARGET_LUX * LUX_DEADBAND_RATIO;
    if (lux_smooth > TARGET_LUX - deadband && lux_smooth < TARGET_LUX + deadband) {

        /* پایدار؛ فقط هر از چندگاه یک لاگ وضعیت برای تست */
        if (++s_ctrl.steady_ticks >= STEADY_LOG_EVERY_TICKS) {
            s_ctrl.steady_ticks = 0;
            ESP_LOGI(TAG, "steady: Lux=%.0f target=%.0f -> level=%d",
                     lux_smooth, TARGET_LUX, s_ctrl.level);
        }
        return;
    }

    /* گام تطبیقی: هر تیک یک گام به سمت هدف؛ دور از هدف گام بزرگ، نزدیک هدف گام ریز.
     * پرش ناگهانی 0↔1023 ممنوع: سهم تمام‌قد LED خیلی بیشتر از نوار مرگی است
     * و سیستم را به نوسان روشن/خاموش می‌اندازد. */
    const int step = adaptive_step(lux_smooth);
    int new_level;
    if (lux_smooth < TARGET_LUX) {
        new_level = s_ctrl.level + step; /* تاریک‌تر از هدف: روشن‌تر کن */
        if (new_level > LIGHT_LEVEL_MAX) {
            new_level = LIGHT_LEVEL_MAX;
        }
    } else {
        new_level = s_ctrl.level - step; /* روشن‌تر از هدف: کم کن */
        if (new_level < 0) {
            new_level = 0;
        }
    }
    apply_level(new_level, lux_smooth);
}

/**
 * @brief callback سنسور BH1750 (هر 2 ثانیه از تسک bh1750): کنترل همین‌جا اجرا می‌شود
 */
static void on_light_reading(const bh1750_reading_t *reading, void *context)
{
    light_ctrl_t *ctrl = context;

    if (!ctrl->saw_first_reading) {
        ctrl->saw_first_reading = true;
        ESP_LOGI(TAG, "First lux reading received, starting light control");
    }

    control_tick(reading->lux);
}

esp_err_t lighting_manager_init(void)
{
    esp_err_t err = light_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "light_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bh1750_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bh1750_init failed: %s", esp_err_to_name(err));
        return err;
    }
    bh1750_set_reading_callback(on_light_reading, &s_ctrl);

    ESP_LOGI(TAG, "Lighting manager initialized (target=%.0f lux)", TARGET_LUX);
    return ESP_OK;
}
