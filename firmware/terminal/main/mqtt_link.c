#include "mqtt_link.h"

#include <stdatomic.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "net.h"

static const char *TAG = "mqtt";

// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/csprd02/mqtt-v3.1.1-csprd02.pdf
#define MQTT_KEEPALIVE_S 15 // broker wykryje zamk. po KEEPALIVE x 1.5

static esp_mqtt_client_handle_t s_client;
static atomic_bool s_connected;
static char s_dev_id[13];       // MAC w/o " : " + '\0' = 13
static char s_topic_status[48]; // "terminal/<dev>/status"

static bool s_error_logged; // blad laczenia juz zgloszony tylko zadanie MQTT - jeden wlasciciel

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch ((esp_mqtt_event_id_t)id)
    {
        case MQTT_EVENT_CONNECTED:
            s_error_logged = false; // nastepny problem znow zostanie zgloszony
            atomic_store(&s_connected, true);
            ESP_LOGI(TAG, "Polaczono z brokerem");
            esp_mqtt_client_publish(s_client, s_topic_status, "online", 0, 1, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            if (atomic_exchange(&s_connected, false)) // TYLKO log dla ZMIANY
            {
                ESP_LOGW(TAG, "Rozlaczono z brokerem - ponowenie automatyczne");
            }
            break;
        case MQTT_EVENT_ERROR:
        {
            const esp_mqtt_event_handle_t ev = data;
            if (atomic_load(&s_connected) || !net_is_connected() || s_error_logged)
            {
                break; // zerwanie zglasza DISCONNECTED  brak Wi-Fi zglasza net raz wystarczy
            }
            if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            {
                ESP_LOGE(TAG, "Broker odmowil polaczenia (kod %d)",
                         ev->error_handle->connect_return_code);
            }
            else
            {
                ESP_LOGW(TAG, "Broker nieosiagalny: %s - ponawianie w tle",
                         esp_err_to_name(ev->error_handle->esp_tls_last_esp_err));
            }
            s_error_logged = true;
            break;
        }
        default:
            break;
    }
}

esp_err_t mqtt_link_init(void)
{
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "odczyt MAC");
    snprintf(s_dev_id, sizeof(s_dev_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    snprintf(s_topic_status, sizeof(s_topic_status), "terminal/%s/status", s_dev_id);

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri       = CONFIG_TERMINAL_MQTT_URI,
        .credentials.client_id    = s_dev_id, // 1 klient staly identyfikator
        .session.keepalive        = MQTT_KEEPALIVE_S,
        .session.last_will.topic  = s_topic_status,
        .session.last_will.msg    = "offline",
        .session.last_will.qos    = 1,
        .session.last_will.retain = 1,
    };

    s_client = esp_mqtt_client_init(&cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "esp_mqtt_client_init");
    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL), TAG,
        "rejestracja zdarzen");

    // biblioteki loguja kazda nieudana probe 3 linie co ~25 s bez brokera
    // stan zglasza  handler - raz + przyczyna
    esp_log_level_set("mqtt_client", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);

    ESP_RETURN_ON_ERROR(esp_mqtt_client_start(s_client), TAG, "start klienta");
    ESP_LOGI(TAG, "Terminal %s, broker %s", s_dev_id, CONFIG_TERMINAL_MQTT_URI);
    return ESP_OK;
}

bool mqtt_link_is_connected()
{
    return atomic_load(&s_connected);
}