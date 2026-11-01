#include "net.h"

#include <string.h>

#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char *TAG = "net";

#define NET_RETRY_DELAY_MS 5000
#define NET_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;

static void net_retry_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const wifi_event_sta_disconnected_t *ev = data;
        xEventGroupClearBits(s_events, NET_CONNECTED_BIT);
        ESP_LOGW(TAG, "Brak polaczenia (reason: %d), ponowna proba za %d s", ev->reason,
                 NET_RETRY_DELAY_MS / 1000);
        (void)esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)NET_RETRY_DELAY_MS * 1000);
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "Polaczono, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, NET_CONNECTED_BIT);
    }
}
esp_err_t net_init(void)
{
    ESP_RETURN_ON_FALSE(strlen(CONFIG_TERMINAL_WIFI_SSID) > 0, ESP_ERR_INVALID_STATE, TAG,
                        "brak SSID - ustawienie w menuconfig (Terminal -siec)");

    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events != NULL, ESP_ERR_NO_MEM, TAG, "xEventGroupCreate");

    const esp_timer_create_args_t retry_args = {
        .callback = net_retry_cb,
        .name     = "net_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer), TAG, "retry timer");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            net_event_handler, NULL, NULL),
                        TAG, "register WIFI_EVENT");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            net_event_handler, NULL, NULL),
                        TAG, "register IP_EVENT");

    wifi_config_t wcfg = {
        .sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,
    };

    strlcpy((char *)wcfg.sta.ssid, CONFIG_TERMINAL_WIFI_SSID, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, CONFIG_TERMINAL_WIFI_PASSWORD, sizeof(wcfg.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wcfg), TAG, "set_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");
    return ESP_OK;
}

bool net_is_connected(void)
{
    return (xEventGroupGetBits(s_events) & NET_CONNECTED_BIT) != 0;
}