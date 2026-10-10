#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float lux;
    } bh1750_sensor_reading_t;
    typedef void (*bh1750_sensor_reading_callback_t)(const bh1750_sensor_reading_t *reading, void *context);
    esp_err_t bh1750_sensor_init(void);
    void bh1750_sensor_set_reading_callback(bh1750_sensor_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif