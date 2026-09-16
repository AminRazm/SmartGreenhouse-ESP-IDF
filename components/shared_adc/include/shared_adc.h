#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief هندل مشترک ADC1 را برمی‌گرداند (فقط بار اول ساخته می‌شود)
 *
 * درایور adc_oneshot در ESP-IDF هر یونیت ADC را فقط یک بار قابل claim می‌کند
 * (فراخوانی دوم adc_oneshot_new_unit برای همان یونیت ESP_ERR_NOT_FOUND
 * برمی‌گرداند)، پس همه‌ی سنسورهای آنالوگ باید کانال خود را روی همین هندل
 * مشترک پیکربندی کنند. تابع adc_oneshot_read خودش خواندن‌های هم‌زمان چند
 * تسک را سری می‌کند و در صورت شلوغ بودن ESP_ERR_TIMEOUT می‌دهد.
 *
 * این تابع فقط باید از context مقداردهی اولیه (app_main) فراخوانی شود.
 */
esp_err_t shared_adc1_get_handle(adc_oneshot_unit_handle_t *out_handle);

#ifdef __cplusplus
}
#endif