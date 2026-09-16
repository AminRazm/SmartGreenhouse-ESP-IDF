#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief راه‌اندازی پریفرال LEDC برای کنترل نوار LED با PWM (شروع: خاموش)
 */
esp_err_t light_init(void);

/**
 * @brief تنظیم مستقیم سطح روشنایی با واحد خام PWM (بدون هیچ تبدیل درصد)
 *
 * @param level مقدار 0 (خاموش) تا 1023 (بیشینه، 10 بیت)؛
 *              مقادیر خارج از بازه به نزدیک‌ترین حد مجاز محدود می‌شوند.
 */
esp_err_t light_set_level(int level);

#ifdef __cplusplus
}
#endif
