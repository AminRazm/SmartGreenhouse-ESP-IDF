#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int average_value;
} soil_moisture_sensor_reading_t;

typedef void (*soil_moisture_sensor_reading_callback_t)(const soil_moisture_sensor_reading_t *reading, void *context);

esp_err_t soil_moisture_sensor_init(void);

void soil_moisture_sensor_set_reading_callback(soil_moisture_sensor_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif
