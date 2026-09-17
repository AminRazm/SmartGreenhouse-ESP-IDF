#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    STATUS_LED_PATTERN_OFF = 0,        
    STATUS_LED_PATTERN_TANK_EMPTY,     
    STATUS_LED_PATTERN_PUMP_ACTIVE,    
} status_led_actuator_pattern_id_t;


esp_err_t status_led_actuator_init(void);


void status_led_actuator_set_pattern(status_led_actuator_pattern_id_t id);

#ifdef __cplusplus
}
#endif
