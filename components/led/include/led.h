#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief الگوهای چشمک ال ای دی رویبرد
 */
typedef enum {
    LED_PATTERN_OFF = 0,        /*!< خاموش (حالت عادی) */
    LED_PATTERN_TANK_EMPTY,     /*!< چشمک آبی کم‌نور هر 1 ثانیه = مخزن خالی */
    LED_PATTERN_PUMP_ACTIVE,    /*!< چشمک آبی پرنور هر 100 ms = پمپ روشن */
} led_pattern_id_t;

/**
 * @brief راه‌اندازی ال ای دی رویبرد (WS2812 روی GPIO48 از طریق RMT)
 */
esp_err_t led_init(void);

/**
 * @brief انتخاب الگوی فعلی ال ای دی
 */
void led_set_pattern(led_pattern_id_t id);

#ifdef __cplusplus
}
#endif
