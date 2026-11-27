#include "mqtt_link.h"

#include <stdatomic.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "net.h"

static const char *TAG = "mqtt";

// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/csprd02/mqtt-v3.1.1-csprd02.pdf
#define MQTT_KEEPALIVE_S 15 // broker wykryje zamk. po KEEPALIVE x 1.5

static esp_mqtt_client_handle_t s_client;
static atomic_bool s_connected;
static char s_dev_id[13];         // MAC w/o " : " + '\0' = 13
static char s_topic_status[48];   // "terminal/<dev>/status"
static char s_topic_events[48];   // "terminal/<dev>/events"
static char s_topic_ack[48];      // "terminal/<dev>/ack"
static QueueHandle_t s_ack_queue; // numery seq z ACK: zadanie MQTT->app_main

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
            esp_mqtt_client_subscribe(s_client, s_topic_ack, 1); // po kazdym polaczeniu
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
        case MQTT_EVENT_DATA:
        {
            const esp_mqtt_event_handle_t ev = data;
            // uwaga: topic i data nie sa zakonczone '\0'
            // uzywac dlugosci
            if (ev->topic_len != (int)strlen(s_topic_ack) ||
                memcmp(ev->topic, s_topic_ack, ev->topic_len) != 0 ||
                ev->data_len != ev->total_data_len || ev->data_len >= 16)
            {
                break; // to nie jest oczekiwane potwierdzenie
            }
            char buf[16];
            memcpy(buf, ev->data, ev->data_len);
            buf[ev->data_len] = '\0';

            char *end;
            unsigned long seq = strtoul(buf, &end, 10);
            if (end == buf || *end != '\0')
            {
                ESP_LOGW(TAG, "Niepoprawny ACK: '%s'", buf);
                break;
            }
            uint32_t s = (uint32_t)seq;
            xQueueSend(s_ack_queue, &s, 0); // dla pelnej kolejki ACK przepadnie
            // terminal wyśle ponownie
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
    snprintf(s_topic_events, sizeof(s_topic_events), "terminal/%s/events", s_dev_id);
    snprintf(s_topic_ack, sizeof(s_topic_ack), "terminal/%s/ack", s_dev_id);

    s_ack_queue = xQueueCreate(8, sizeof(uint32_t));
    ESP_RETURN_ON_FALSE(s_ack_queue != NULL, ESP_ERR_NO_MEM, TAG, "kolejka ACK");

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

esp_err_t mqtt_link_publish_event(const char *json)
{
    // QoS1, no retain - zdarznie != stan
    // 0 - streln liczy length
    int msg_id = esp_mqtt_client_publish(s_client, s_topic_events, json, 0, 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

bool mqtt_link_get_ack(uint32_t *seq)
{
    return xQueueReceive(s_ack_queue, seq, 0) == pdTRUE; // 0 = no wait
}