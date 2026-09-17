#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t light_actuator_init(void);


esp_err_t light_actuator_set_level(int level);

#ifdef __cplusplus
}
#endif
