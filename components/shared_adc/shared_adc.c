#include "shared_adc.h"

#include "esp_log.h"

static const char *TAG = "shared_adc";

static adc_oneshot_unit_handle_t s_adc1_handle;

esp_err_t shared_adc1_get_handle(adc_oneshot_unit_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_adc1_handle == NULL) {
        const adc_oneshot_unit_init_cfg_t unit_config = {
            .unit_id = ADC_UNIT_1,
        };
        const esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc1_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_new_unit(ADC_UNIT_1) failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    *out_handle = s_adc1_handle;
    return ESP_OK;
}