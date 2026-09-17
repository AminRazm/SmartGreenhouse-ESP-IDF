#include "wifi_manager.h"
#include "wifiSecrets.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#define NTP_SERVER "ntp.time.ir"
#define TIMEZONE "IRST-3:30"
#define WIFI_MAX_RETRY_ATTEMPTS 6

static const char *TAG = "wifi_manager";
static uint32_t reconnect_attempts = 0;
static esp_timer_handle_t reconnect_timer = NULL;
static EventGroupHandle_t s_time_events;

#define TIME_SYNCED_BIT BIT0

static esp_err_t check_error(esp_err_t err, const char *operation)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    }
    return err;
}

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
    xEventGroupSetBits(s_time_events, TIME_SYNCED_BIT);
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
            const esp_err_t err = esp_timer_start_once(reconnect_timer, timeout_us);
            check_error(err, "esp_timer_start_once");
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


        esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SNTP start failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "SNTP started (server: " NTP_SERVER ")");
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    s_time_events = xEventGroupCreate();
    if (s_time_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    err = check_error(err, "nvs_flash_init");
    if (err != ESP_OK) {
        return err;
    }
    err = check_error(esp_netif_init(), "esp_netif_init");
    if (err != ESP_OK) {
        return err;
    }
    err = check_error(esp_event_loop_create_default(), "esp_event_loop_create_default");
    if (err != ESP_OK) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = check_error(esp_wifi_init(&init_config), "esp_wifi_init");
    if (err != ESP_OK) {
        return err;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = &reconnect_timer_cb,
        .name = "wifi_reconnect_timer"};
    err = check_error(esp_timer_create(&timer_args, &reconnect_timer), "esp_timer_create");
    if (err != ESP_OK) {
        return err;
    }
    err = check_error(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL,
        NULL), "register Wi-Fi event handler");
    if (err != ESP_OK) {
        return err;
    }
    err = check_error(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL,
        NULL), "register IP event handler");
    if (err != ESP_OK) {
        return err;
    }
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD}};
    err = check_error(esp_wifi_set_mode(WIFI_MODE_STA), "esp_wifi_set_mode");
    if (err != ESP_OK) {
        return err;
    }
    err = check_error(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), "esp_wifi_set_config");
    if (err != ESP_OK) {
        return err;
    }
    setenv("TZ", TIMEZONE, 1);
    tzset();
    esp_sntp_config_t sntp_config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    sntp_config.start = false; 
    sntp_config.sync_cb = sntp_sync_callback;
    err = check_error(esp_netif_sntp_init(&sntp_config), "esp_netif_sntp_init");
    if (err != ESP_OK) {
        return err;
    }
    return check_error(esp_wifi_start(), "esp_wifi_start");
}

bool wifi_manager_wait_for_time_sync(uint32_t timeout_ms)
{
    if (s_time_events == NULL) {
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_time_events,
        TIME_SYNCED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & TIME_SYNCED_BIT) != 0;
}
