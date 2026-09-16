#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FAN_SPEED_OFF = 0,
    FAN_SPEED_LOW,
    FAN_SPEED_MEDIUM,
    FAN_SPEED_HIGH,
} fan_speed_t;

esp_err_t fan_init(void);

/* سرعت با duty خام 10 بیتی اعمال می‌شود (OFF=0 / LOW=400 / MEDIUM=700 / HIGH=1023) */
esp_err_t fan_control_set_speed(fan_speed_t speed);

#ifdef __cplusplus
}
#endif