#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief راه‌اندازی کامل سیستم مدیریت آب‌وهوای گلخانه
 *
 * این تابع مسئول راه‌اندازی سنسورهای دما/رطوبت (dht22)، سنسور گاز (mq135)
 * و کنترل‌کننده فن (fan_control) است. همچنین callback های لازم را برای
 * دریافت خودکار داده از سنسورها ثبت می‌کند تا سرعت فن به‌صورت خودکار
 * بر اساس آخرین مقادیر خوانده‌شده تنظیم شود.
 *
 * @return ESP_OK در صورت موفقیت در راه‌اندازی همه زیرسیستم‌ها
 */
esp_err_t climate_manager_init(void);

#ifdef __cplusplus
}
#endif