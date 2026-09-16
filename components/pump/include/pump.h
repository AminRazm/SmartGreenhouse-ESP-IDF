#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief راه‌اندازی GPIO مسفت پمپ آب (خاموش در حالت اولیه)
 */
esp_err_t pump_init(void);

/**
 * @brief پمپ را برای مدت مشخصی (PUMP_ON_DURATION_MS = ۱۰ ثانیه) روشن می‌کند
 *
 * این تابع non-blocking است: بلافاصله برمی‌گردد و یک تایمر نرم‌افزاری
 * (esp_timer) داخلی، پس از پایان مدت‌زمان، خودش پمپ را خاموش می‌کند.
 * اگر پمپ در حال حاضر مشغول باشد، این تابع بدون اثر عمل کرده و
 * ESP_ERR_INVALID_STATE برمی‌گرداند.
 *
 * @return ESP_OK اگر پالس آبیاری با موفقیت شروع شد
 */
esp_err_t pump_start_pulse(void);

/**
 * @brief خاموش کردن فوری پمپ و لغو پالس جاری
 *
 * اگر پالسی در جریان باشد، تایمر آن لغو و پمپ بلافاصله خاموش می‌شود.
 * اگر پمپ روشن نباشد، این تابع بی‌اثر است.
 */
void pump_stop(void);

/**
 * @brief آیا پمپ در حال حاضر (در وسط یک پالس آبیاری) روشن است؟
 */
bool pump_is_running(void);

#ifdef __cplusplus
}
#endif