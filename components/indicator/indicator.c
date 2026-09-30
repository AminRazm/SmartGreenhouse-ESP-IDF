#include "indicator.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INDICATOR_GPIO GPIO_NUM_48
#define INDICATOR_RESOLUTION_HZ (20 * 1000 * 1000)
#define INDICATOR_QUEUE_DEPTH 1

#define WS2812_T1H_TICKS 16
#define WS2812_T1L_TICKS 9
#define WS2812_T0H_TICKS 8
#define WS2812_T0L_TICKS 17

static const char *TAG = "indicator";

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t on_ms;
    uint32_t off_ms;
} indicator_pattern_t;

static rmt_symbol_word_t s_symbol_one;
static rmt_symbol_word_t s_symbol_zero;
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static volatile uint32_t s_active_events;
static portMUX_TYPE s_events_lock = portMUX_INITIALIZER_UNLOCKED;

static const rmt_transmit_config_t s_transmit_config = {
    .loop_count = 0,
    .flags.eot_level = 0,
};

static size_t encode_ws2812(const void *data, size_t data_size,
                            size_t symbols_written, size_t symbols_free,
                            rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    (void)arg;

    const uint8_t *bytes = data;
    const size_t total_bits = data_size * 8;
    size_t count = 0;

    while (symbols_written + count < total_bits && count < symbols_free) {
        const size_t bit_index = symbols_written + count;
        const bool bit = (bytes[bit_index / 8] & (0x80 >> (bit_index % 8))) != 0;
        symbols[count++] = bit ? s_symbol_one : s_symbol_zero;
    }

    *done = symbols_written + count >= total_bits;
    return count;
}

static void show_color(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint8_t grb[3] = {green, red, blue};
    ESP_ERROR_CHECK(rmt_transmit(s_channel, s_encoder, grb, sizeof(grb),
                                 &s_transmit_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_channel, 100));
}

static uint32_t get_active_events(void)
{
    uint32_t events;
    taskENTER_CRITICAL(&s_events_lock);
    events = s_active_events;
    taskEXIT_CRITICAL(&s_events_lock);
    return events;
}

static indicator_pattern_t get_pattern(void)
{
    const uint32_t events = get_active_events();

    if (events & (1U << INDICATOR_EVENT_CRITICAL)) {
        return (indicator_pattern_t){255, 0, 0, 100, 100};
    }
    if (events & (1U << INDICATOR_EVENT_SOIL_SENSOR_ERROR)) {
        return (indicator_pattern_t){255, 0, 0, 200, 800};
    }
    if (events & (1U << INDICATOR_EVENT_WIFI_DISCONNECTED)) {
        return (indicator_pattern_t){255, 0, 0, 250, 1750};
    }
    if (events & (1U << INDICATOR_EVENT_TIME_NOT_SYNCED)) {
        return (indicator_pattern_t){255, 150, 0, 250, 1750};
    }
    if (events & (1U << INDICATOR_EVENT_SOIL_DRY)) {
        return (indicator_pattern_t){255, 70, 0, 300, 700};
    }
    if (events & (1U << INDICATOR_EVENT_PUMP_ACTIVE)) {
        return (indicator_pattern_t){0, 0, 255, 100, 100};
    }
    if (events & (1U << INDICATOR_EVENT_STARTUP)) {
        return (indicator_pattern_t){0, 0, 255, 150, 850};
    }

    return (indicator_pattern_t){0, 0, 0, 100, 1900};
}

static void indicator_task(void *arg)
{
    (void)arg;

    for (;;) {
        const indicator_pattern_t pattern = get_pattern();
        show_color(pattern.red, pattern.green, pattern.blue);
        vTaskDelay(pdMS_TO_TICKS(pattern.on_ms));
        show_color(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(pattern.off_ms));
    }
}

esp_err_t indicator_init(void)
{
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = INDICATOR_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = INDICATOR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = INDICATOR_QUEUE_DEPTH,
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
        .callback = encode_ws2812,
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

    const BaseType_t created = xTaskCreate(indicator_task, "indicator", 3072, NULL, 2, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "RGB indicator initialized on GPIO%d", INDICATOR_GPIO);
    return ESP_OK;
}

void indicator_set_event(indicator_event_t event, bool active)
{
    if (event < 0 || event >= INDICATOR_EVENT_COUNT) {
        return;
    }

    taskENTER_CRITICAL(&s_events_lock);
    if (active) {
        s_active_events |= 1U << event;
    } else {
        s_active_events &= ~(1U << event);
    }
    taskEXIT_CRITICAL(&s_events_lock);
}
