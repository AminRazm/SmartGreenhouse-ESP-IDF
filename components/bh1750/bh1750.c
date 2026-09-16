#include "bh1750.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------- تنظیمات قابل تغییر ---------- */
#define BH1750_I2C_PORT       I2C_NUM_0
#define BH1750_SDA_GPIO       GPIO_NUM_8
#define BH1750_SCL_GPIO       GPIO_NUM_9
#define BH1750_I2C_FREQUENCY_HZ 100000
#define BH1750_I2C_ADDRESS    0x23  /* آدرس وقتی پایه ADDR به GND وصل باشد */

#define BH1750_CMD_POWER_ON        0x01
#define BH1750_CMD_CONTINUOUS_HRES 0x10  /* اندازه‌گیری پیوسته با دقت بالا (1 lux) */

#define BH1750_READ_INTERVAL_MS 2000  /* حداقل ~120ms طول می‌کشد تا اندازه‌گیری آماده شود؛ 2 ثانیه فاصله امن است */

static const char *TAG = "bh1750";

static i2c_master_bus_handle_t s_bus_handle;
static i2c_master_dev_handle_t s_dev_handle;

static bh1750_reading_callback_t s_callback;
static void *s_callback_ctx;

/**
 * @brief یک اندازه‌گیری خام از سنسور می‌خواند و آن را به لوکس تبدیل می‌کند
 */
static esp_err_t bh1750_read_lux(float *lux)
{
    uint8_t raw_data[2];
    esp_err_t err = i2c_master_receive(s_dev_handle, raw_data, sizeof(raw_data), -1);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t raw_value = ((uint16_t)raw_data[0] << 8) | raw_data[1];
    /* طبق datasheet BH1750: lux = raw_value / 1.2 در حالت دقت بالا */
    *lux = raw_value / 1.2f;
    return ESP_OK;
}

static void bh1750_task(void *arg)
{
    (void)arg;

    /* اولین اندازه‌گیری در حالت HRES حدود 180ms طول می‌کشد؛ قبل از اولین
     * خواندن صبر می‌کنیم تا به‌جای مقدار صفرِ ثبات، داده‌ی واقعی بخوانیم. */
    vTaskDelay(pdMS_TO_TICKS(200));

    while (true) {
        float lux;
        const esp_err_t err = bh1750_read_lux(&lux);

        if (err == ESP_OK) {
            if (s_callback != NULL) {
                const bh1750_reading_t reading = { .lux = lux };
                s_callback(&reading, s_callback_ctx);
            }
        } else {
            ESP_LOGW(TAG, "Read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(BH1750_READ_INTERVAL_MS));
    }
}

esp_err_t bh1750_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BH1750_I2C_PORT,
        .sda_io_num = BH1750_SDA_GPIO,
        .scl_io_num = BH1750_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (err != ESP_OK) {
        return err;
    }

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_I2C_ADDRESS,
        .scl_speed_hz = BH1750_I2C_FREQUENCY_HZ,
    };
    err = i2c_master_bus_add_device(s_bus_handle, &dev_config, &s_dev_handle);
    if (err != ESP_OK) {
        return err;
    }

    /* روشن کردن سنسور و انتخاب حالت اندازه‌گیری پیوسته با دقت بالا */
    const uint8_t power_on_cmd = BH1750_CMD_POWER_ON;
    err = i2c_master_transmit(s_dev_handle, &power_on_cmd, 1, -1);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t mode_cmd = BH1750_CMD_CONTINUOUS_HRES;
    err = i2c_master_transmit(s_dev_handle, &mode_cmd, 1, -1);
    if (err != ESP_OK) {
        return err;
    }

    const BaseType_t created = xTaskCreate(bh1750_task, "bh1750_task", 3072, NULL, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void bh1750_set_reading_callback(bh1750_reading_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_ctx = context;
}