#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t yl69_sensor_init(void);
esp_err_t yl69_sensor_set_power(bool enabled);
esp_err_t yl69_sensor_read_raw(int *raw_value);

#ifdef __cplusplus
}
#endif
