#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief یک نمونه خوانده‌شده از سنسور نور BH1750
 */
typedef struct {
    float lux; /*!< میزان نور بر حسب لوکس */
} bh1750_reading_t;

typedef void (*bh1750_reading_callback_t)(const bh1750_reading_t *reading, void *context);

/**
 * @brief راه‌اندازی باس I2C و شروع تسک خواندن دوره‌ای سنسور BH1750
 *
 * @return ESP_OK در صورت موفقیت، در غیر این صورت کد خطا
 */
esp_err_t bh1750_init(void);

/**
 * @brief ثبت تابعی که با هر خواندن موفق سنسور فراخوانی شود
 */
void bh1750_set_reading_callback(bh1750_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif