#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATER_TANK_STATUS_UNKNOWN = 0,
    WATER_TANK_STATUS_HAS_WATER = 1,
    WATER_TANK_STATUS_EMPTY = 2,
} water_tank_status_t;

esp_err_t irrigation_controller_init(void);

water_tank_status_t irrigation_controller_get_tank_status(void);

#ifdef __cplusplus
}
#endif
