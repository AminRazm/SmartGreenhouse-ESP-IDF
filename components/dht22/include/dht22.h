#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief یک نمونه خوانده‌شده از سنسور DHT22
 */
typedef struct {
    float temperature; /*!< دما بر حسب درجه سانتی‌گراد */
    float humidity;    /*!< رطوبت نسبی بر حسب درصد */
} dht22_reading_t;

/**
 * @brief نوع تابع callback که هر بار خواندن موفق سنسور فراخوانی می‌شود
 *
 * @param reading اشاره‌گر به آخرین مقدار خوانده‌شده (فقط در طول اجرای callback معتبر است)
 * @param context همان context ای که هنگام ثبت callback داده شده بود
 */
typedef void (*dht22_reading_callback_t)(const dht22_reading_t *reading, void *context);

/**
 * @brief راه‌اندازی سنسور DHT22 و شروع تسک خواندن دوره‌ای آن
 *
 * این تابع یک تسک FreeRTOS ایجاد می‌کند که هر DHT22_READ_INTERVAL_MS
 * یک بار سنسور را می‌خواند و در صورت موفقیت، callback ثبت‌شده را صدا می‌زند.
 * فراخوانی این تابع non-blocking است.
 *
 * @return ESP_OK در صورت موفقیت، در غیر این صورت کد خطا
 */
esp_err_t dht22_init(void);

/**
 * @brief ثبت تابعی که با هر خواندن موفق سنسور فراخوانی شود
 *
 * @param callback تابعی که باید صدا زده شود (NULL برای حذف callback فعلی)
 * @param context اشاره‌گری دلخواه که بدون تغییر به callback پاس داده می‌شود
 */
void dht22_set_reading_callback(dht22_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif