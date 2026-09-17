#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    int raw_value; 
} mq135_sensor_reading_t;


typedef void (*mq135_sensor_reading_callback_t)(const mq135_sensor_reading_t *reading, void *context);


esp_err_t mq135_sensor_init(void);


void mq135_sensor_set_reading_callback(mq135_sensor_reading_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif