#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INDICATOR_EVENT_WIFI_DISCONNECTED = 0,
    INDICATOR_EVENT_TIME_NOT_SYNCED,
    INDICATOR_EVENT_SOIL_DRY,
    INDICATOR_EVENT_PUMP_ACTIVE,
    INDICATOR_EVENT_CRITICAL,
    INDICATOR_EVENT_COUNT,
} indicator_event_t;

esp_err_t indicator_init(void);

void indicator_set_event(indicator_event_t event, bool active);

#ifdef __cplusplus
}
#endif
