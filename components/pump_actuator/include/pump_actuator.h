#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t pump_actuator_init(void);


esp_err_t pump_actuator_start_pulse(void);


void pump_actuator_stop(void);


bool pump_actuator_is_running(void);

#ifdef __cplusplus
}
#endif