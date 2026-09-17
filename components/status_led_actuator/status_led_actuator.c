#include "status_led_actuator.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/clk_tree_defs.h"




#define STATUS_LED_GPIO  GPIO_NUM_48
#define STATUS_LED_COUNT 1


#define STATUS_LED_OFF_PATTERN         {  1,  999,   0 }
#define STATUS_LED_TANK_EMPTY_PATTERN  { 100,  900,  60 }
#define STATUS_LED_PUMP_ACTIVE_PATTERN {  50,   50, 255 }


#define WS2812_T1H_TICKS 16 
#define WS2812_T1L_TICKS 9  
#define WS2812_T0H_TICKS 8  
#define WS2812_T0L_TICKS 17 

static const char *TAG = "status_led_actuator";

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    uint8_t blue;
} status_led_actuator_pattern_t;


static rmt_symbol_word_t s_symbol_one;
static rmt_symbol_word_t s_symbol_zero;

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static volatile status_led_actuator_pattern_t s_pattern = STATUS_LED_OFF_PATTERN;


static const rmt_transmit_config_t s_tx_config = {
    .loop_count = 0,
    .flags.eot_level = 0,
};


static size_t ws2812_encode(const void *data, size_t data_size,
                            size_t symbols_written, size_t symbols_free,
                            rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    (void)arg;

    const uint8_t *bytes = data;
    const size_t total_bits = data_size * 8;
    size_t count = 0;

    while (symbols_written + count < total_bits && count < symbols_free) {

        const size_t bit_index = symbols_written + count;
        const bool bit = bytes[bit_index / 8] & (0x80 >> (bit_index % 8));

        symbols[count] = bit ? s_symbol_one : s_symbol_zero;
        ++count;
    }

    if (symbols_written + count >= total_bits) {
        *done = true; 
    }

    return count;
}


static void status_led_actuator_show(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint8_t color[3] = { green, red, blue };

    rmt_transmit(s_channel, s_encoder, color, sizeof(color), &s_tx_config);
    rmt_tx_wait_all_done(s_channel, 100); 
}

static void status_led_actuator_task(void *arg)
{
    (void)arg;

    for (;;) {

        const status_led_actuator_pattern_t pattern = s_pattern;

        if (pattern.blue > 0) {
            status_led_actuator_show(0, 0, pattern.blue);
        }

        vTaskDelay(pdMS_TO_TICKS(pattern.on_ms));

        status_led_actuator_show(0, 0, 0); 

        vTaskDelay(pdMS_TO_TICKS(pattern.off_ms));
    }
}

esp_err_t status_led_actuator_init(void)
{
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = STATUS_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 20 * 1000 * 1000, 
        .mem_block_symbols = 64, 
        .trans_queue_depth = 1,
    };

    esp_err_t err = rmt_new_tx_channel(&channel_config, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    s_symbol_one = (rmt_symbol_word_t){
        .duration0 = WS2812_T1H_TICKS, .level0 = 1,
        .duration1 = WS2812_T1L_TICKS, .level1 = 0,
    };
    s_symbol_zero = (rmt_symbol_word_t){
        .duration0 = WS2812_T0H_TICKS, .level0 = 1,
        .duration1 = WS2812_T0L_TICKS, .level1 = 0,
    };

    const rmt_simple_encoder_config_t encoder_config = {
        .callback = ws2812_encode,
        .min_chunk_size = 32, 
    };

    err = rmt_new_simple_encoder(&encoder_config, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_simple_encoder failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    status_led_actuator_show(0, 0, 0); 

    const BaseType_t created = xTaskCreate(status_led_actuator_task, "status_led_actuator_task", 2048, NULL, 2, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LED initialized on GPIO%d (WS2812 over RMT)", STATUS_LED_GPIO);
    return ESP_OK;
}

void status_led_actuator_set_pattern(status_led_actuator_pattern_id_t id)
{
    static const status_led_actuator_pattern_t patterns[] = {
        [STATUS_LED_PATTERN_OFF]         = STATUS_LED_OFF_PATTERN,
        [STATUS_LED_PATTERN_TANK_EMPTY]  = STATUS_LED_TANK_EMPTY_PATTERN,
        [STATUS_LED_PATTERN_PUMP_ACTIVE] = STATUS_LED_PUMP_ACTIVE_PATTERN,
    };

    if ((size_t)id < sizeof(patterns) / sizeof(patterns[0])) {
        s_pattern = patterns[id];
    }
}
