#include "dht22_sensor.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define DHT22_GPIO                 GPIO_NUM_7
#define DHT22_READ_INTERVAL_MS     3000
#define DHT22_POWER_UP_DELAY_MS    2000

#define DHT22_START_SIGNAL_LOW_MS  10
#define DHT22_START_SIGNAL_HIGH_US 40


#define DHT22_ACK_TIMEOUT_US       5000
#define DHT22_PHASE_TIMEOUT_US     500
#define DHT22_BIT_ONE_THRESHOLD_US 50

#define DHT22_MAX_RETRIES          3
#define DHT22_RETRY_GAP_MS         100


#define DHT22_CONSECUTIVE_FAIL_WARN 5

static const char *TAG = "dht22_sensor";

static dht22_sensor_reading_callback_t s_callback;
static void *s_callback_ctx;


static esp_err_t wait_for_level(int level, uint32_t timeout_us)
{
    const int64_t deadline = esp_timer_get_time() + timeout_us;

    while (gpio_get_level(DHT22_GPIO) != level) {
        if (esp_timer_get_time() >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        taskYIELD(); 
    }
    return ESP_OK;
}


static esp_err_t send_start_signal_and_wait_response(void)
{

    gpio_set_direction(DHT22_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(DHT22_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT22_START_SIGNAL_LOW_MS));
    gpio_set_level(DHT22_GPIO, 1);
    esp_rom_delay_us(DHT22_START_SIGNAL_HIGH_US);


    gpio_set_direction(DHT22_GPIO, GPIO_MODE_INPUT);


    if (wait_for_level(0, DHT22_ACK_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(1, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(0, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}


static esp_err_t read_bit(bool *bit_value)
{

    if (wait_for_level(1, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    const int64_t high_start = esp_timer_get_time();


    if (wait_for_level(0, DHT22_PHASE_TIMEOUT_US) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    *bit_value = (esp_timer_get_time() - high_start) > DHT22_BIT_ONE_THRESHOLD_US;
    return ESP_OK;
}


static esp_err_t read_raw_data(uint8_t data[5])
{
    for (int byte_idx = 0; byte_idx < 5; ++byte_idx) {
        data[byte_idx] = 0;
        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
            bool bit_value = false;
            if (read_bit(&bit_value) != ESP_OK) {
                return ESP_ERR_TIMEOUT;
            }
            data[byte_idx] = (uint8_t)((data[byte_idx] << 1) | (bit_value ? 1 : 0));
        }
    }
    return ESP_OK;
}


static bool checksum_is_valid(const uint8_t data[5])
{
    return (uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4];
}


static void decode_reading(const uint8_t data[5], dht22_sensor_reading_t *reading)
{
    const uint16_t humidity_raw = ((uint16_t)data[0] << 8) | data[1];
    uint16_t temperature_raw = ((uint16_t)data[2] << 8) | data[3];

    const bool is_negative = (temperature_raw & 0x8000U) != 0;
    temperature_raw &= 0x7FFFU;

    reading->humidity = humidity_raw / 10.0f;
    reading->temperature = temperature_raw / 10.0f;
    if (is_negative) {
        reading->temperature = -reading->temperature;
    }
}


static esp_err_t dht22_sensor_read(dht22_sensor_reading_t *reading)
{
    for (int attempt = 0; attempt < DHT22_MAX_RETRIES; ++attempt) {
        esp_err_t err = send_start_signal_and_wait_response();
        if (err == ESP_OK) {
            uint8_t data[5] = {0};
            err = read_raw_data(data);
            if (err == ESP_OK && !checksum_is_valid(data)) {
                err = ESP_ERR_INVALID_CRC;
            }
            if (err == ESP_OK) {
                decode_reading(data, reading);
                return ESP_OK;
            }
        }

        if (attempt + 1 < DHT22_MAX_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(DHT22_RETRY_GAP_MS));
        }
    }
    return ESP_ERR_TIMEOUT;
}


static void dht22_sensor_task(void *arg)
{
    (void)arg;


    vTaskDelay(pdMS_TO_TICKS(DHT22_POWER_UP_DELAY_MS));
    ESP_LOGI(TAG, "DHT22 power-up delay done, starting reads");

    int consecutive_failures = 0;

    while (true) {
        dht22_sensor_reading_t reading;
        const esp_err_t err = dht22_sensor_read(&reading);

        if (err == ESP_OK) {
            consecutive_failures = 0;

            if (s_callback != NULL) {
                s_callback(&reading, s_callback_ctx);
            }
        } else {

            consecutive_failures++;
            if (consecutive_failures == DHT22_CONSECUTIVE_FAIL_WARN) {
                ESP_LOGW(TAG, "DHT22 read failed %d times in a row (sensor unplugged?)", consecutive_failures);
            } else if (consecutive_failures > DHT22_CONSECUTIVE_FAIL_WARN &&
                       consecutive_failures % DHT22_CONSECUTIVE_FAIL_WARN == 0) {
                ESP_LOGW(TAG, "DHT22 still failing (%d in a row)", consecutive_failures);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DHT22_READ_INTERVAL_MS));
    }
}

esp_err_t dht22_sensor_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << DHT22_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }


    gpio_set_level(DHT22_GPIO, 1);

    const BaseType_t created = xTaskCreate(dht22_sensor_task, "dht22_sensor_task", 4096, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void dht22_sensor_set_reading_callback(dht22_sensor_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}
