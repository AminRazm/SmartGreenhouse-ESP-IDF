#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        FAN_ACTUATOR_SPEED_OFF = 0,
        FAN_ACTUATOR_SPEED_LOW,
        FAN_ACTUATOR_SPEED_MEDIUM,
        FAN_ACTUATOR_SPEED_HIGH,
    } fan_actuator_speed_t;
    esp_err_t fan_actuator_init(void);
    esp_err_t fan_actuator_set_speed(fan_actuator_speed_t speed);

#ifdef __cplusplus
}
#endif