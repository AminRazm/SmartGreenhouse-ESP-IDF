#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t shared_adc1_get_handle(adc_oneshot_unit_handle_t *out_handle);

#ifdef __cplusplus
}
#endif