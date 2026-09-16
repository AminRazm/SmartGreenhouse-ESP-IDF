#include "wifi.h"
#include "wifiSecrets.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#define NTP_SERVER "ntp.time.ir"
#define TIMEZONE "IRST-3:30"
#define WIFI_MAX_RETRY_ATTEMPTS 6

static const char *TAG = "wifi";
static uint32_t reconnect_attempts = 0;
static esp_timer_handle_t reconnect_timer = NULL;

static void sntp_sync_callback(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);
}

static void reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            /* بک‌آف نمایی تا سقف؛ بعد از سقف هم با همان 32 ثانیه بی‌نهایت تلاش می‌کنیم.
             * SNTP دست نمی‌خوریم؛ با هر IP جدید خودش ری‌استارت می‌شود. */
            const uint32_t backoff_index = (reconnect_attempts < (WIFI_MAX_RETRY_ATTEMPTS - 1))
                                               ? reconnect_attempts
                                               : (WIFI_MAX_RETRY_ATTEMPTS - 1);
            const uint32_t delay_ms = 1000U * (1U << backoff_index);
            ESP_LOGW(TAG, "Wi-Fi disconnected. Will retry in %" PRIu32 " ms", delay_ms);
            if (reconnect_attempts < WIFI_MAX_RETRY_ATTEMPTS) {
                reconnect_attempts++;
            }
            esp_timer_stop(reconnect_timer);
            const uint64_t timeout_us = (uint64_t)delay_ms * 1000;
            ESP_ERROR_CHECK(esp_timer_start_once(reconnect_timer, timeout_us));
            break;
        }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        reconnect_attempts = 0;
        esp_timer_stop(reconnect_timer);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        /* SNTP فقط بعد از قطعیِ داشتن IP راه می‌افتد.
         * esp_netif_sntp_start سرویس قبلی را stop و دوباره init می‌کند؛
         * پس در اتصال‌های مجدد هم فراخوانی‌اش بی‌خطر است. */
        esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SNTP start failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "SNTP started (server: " NTP_SERVER ")");
        }
    }
}

esp_err_t wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    const esp_timer_create_args_t timer_args = {
        .callback = &reconnect_timer_cb,
        .name = "wifi_reconnect_timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reconnect_timer));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL,
        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL,
        NULL));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    setenv("TZ", TIMEZONE, 1);
    tzset();
    esp_sntp_config_t sntp_config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    sntp_config.start = false; /* استارت فقط بعد از دریافت IP */
    sntp_config.sync_cb = sntp_sync_callback;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}
