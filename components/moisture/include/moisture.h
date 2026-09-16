#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int raw_value;      /*!< میانگین نمونه‌های سالم (برای تصمیم آبیاری) */
    int latest_sample;  /*!< آخرین نمونه‌ی خام سالم (برای تشخیص بیرون‌ بودن پراب از خاک) */
} moisture_reading_t;

typedef void (*moisture_reading_callback_t)(const moisture_reading_t *reading, void *context);

/**
 * @brief راه‌اندازی سنسور خازنی رطوبت خاک (نمونه‌گیری هر 1 ثانیه)
 *
 * نمونه‌های پرت (نویز / پراب شناور) فیلتر می‌شوند؛ پرش بزرگ فقط بعد از
 * چند تأیید پشت‌سرهم پذیرفته می‌شود.
 */
esp_err_t moisture_init(void);

/**
 * @brief ثبت callback که با هر نمونه‌ی سالم صدا زده می‌شود
 */
void moisture_set_reading_callback(moisture_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif
