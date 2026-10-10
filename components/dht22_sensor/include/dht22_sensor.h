#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float temperature;
        float humidity;
    } dht22_sensor_reading_t;
    typedef void (*dht22_sensor_reading_callback_t)(const dht22_sensor_reading_t *reading, void *context);
    esp_err_t dht22_sensor_init(void);
    void dht22_sensor_set_reading_callback(dht22_sensor_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif