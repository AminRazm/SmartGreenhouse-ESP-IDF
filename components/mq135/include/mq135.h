#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief یک نمونه (میانگین‌گیری‌شده) خوانده‌شده از سنسور گاز MQ135
 */
typedef struct {
    int raw_value; /*!< میانگین مقدار خام ADC روی چند نمونه اخیر */
} mq135_reading_t;

/**
 * @brief نوع تابع callback که هر بار مقدار میانگین جدید آماده شود فراخوانی می‌شود
 */
typedef void (*mq135_reading_callback_t)(const mq135_reading_t *reading, void *context);

/**
 * @brief راه‌اندازی ADC و شروع تسک نمونه‌برداری/میانگین‌گیری سنسور MQ135
 *
 * @return ESP_OK در صورت موفقیت، در غیر این صورت کد خطا
 */
esp_err_t mq135_init(void);

/**
 * @brief ثبت تابعی که با هر مقدار میانگین جدید فراخوانی شود
 *
 * @param callback تابعی که باید صدا زده شود (NULL برای حذف callback فعلی)
 * @param context اشاره‌گری دلخواه که بدون تغییر به callback پاس داده می‌شود
 */
void mq135_set_reading_callback(mq135_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif