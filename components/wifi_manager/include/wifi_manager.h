#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t wifi_manager_init(void);

/* تا آماده شدن ساعت یا پایان مهلت صبر می‌کند. */
bool wifi_manager_wait_for_time_sync(uint32_t timeout_ms);