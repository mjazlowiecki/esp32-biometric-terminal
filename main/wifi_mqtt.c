#include "wifi_mqtt.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <string.h>

// ==========================================
// WPISZ TU DANE DO SWOJEGO HOTSPOTU Z TELEFONU
// ==========================================
#define WIFI_SSID      "Mój_Hotspot"
#define WIFI_PASS      "MojeHaslo123"

#define BROKER_URL     "mqtt://broker.hivemq.com"
#define MQTT_TOPIC     "smiw/terminal/maciej/logs"
#define MQTT_CMD_TOPIC "smiw/terminal/maciej/cmd" // TEMAT DO ODBIORU

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_mqtt_connected = false;

// Zmienne globalne do komunikatow
volatile bool new_cloud_cmd = false;
char cloud_cmd_buf[64] = {0};

// Inicjalizacja domyslnych aktywnosci (zanim nadejda z chmury)
char dynamic_activities[MAX_ACTIVITIES][MAX_ACTIVITY_NAME_LEN] = {
    "Praca", "Nauka", "Przerwa"
};
int dynamic_activities_count = 3;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("[MQTT] SUKCES! Polaczono z darmowym brokerem HiveMQ!\n");
            is_mqtt_connected = true;
            esp_mqtt_client_subscribe(mqtt_client, MQTT_CMD_TOPIC, 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            printf("[MQTT] Rozlaczono z brokerem.\n");
            is_mqtt_connected = false;
            break;
            
        case MQTT_EVENT_DATA:
            if (strncmp(event->topic, MQTT_CMD_TOPIC, event->topic_len) == 0) {
                char rx_buf[256];
                snprintf(rx_buf, sizeof(rx_buf), "%.*s", event->data_len, event->data);
                
                // Sprawdzamy czy Node-RED przyslal nowa liste aktywnosci
                if (strncmp(rx_buf, "SET:", 4) == 0) {
                    int idx = 0;
                    // Dzielimy string po przecinkach
                    char *token = strtok(rx_buf + 4, ","); 
                    while (token != NULL && idx < MAX_ACTIVITIES) {
                        strncpy(dynamic_activities[idx], token, MAX_ACTIVITY_NAME_LEN - 1);
                        dynamic_activities[idx][MAX_ACTIVITY_NAME_LEN - 1] = '\0';
                        idx++;
                        token = strtok(NULL, ",");
                    }
                    dynamic_activities_count = idx;
                    printf("[MQTT] Zaktualizowano liste aktywnosci! Liczba pozycji: %d\n", dynamic_activities_count);
                    
                    // Pokaz info o aktualizacji na ekranie
                    snprintf(cloud_cmd_buf, sizeof(cloud_cmd_buf), "Aktualizacja listy!");
                    new_cloud_cmd = true;
                } 
                else {
                    // Zwykly komunikat np. "Koniec zmiany"
                    snprintf(cloud_cmd_buf, sizeof(cloud_cmd_buf), "%s", rx_buf);
                    new_cloud_cmd = true; 
                    printf("[MQTT] OTRZYMANO KOMENDE: %s\n", cloud_cmd_buf);
                }
            }
            break;
            
        default:
            break;
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("[WIFI] Utracono polaczenie. Lącze ponownie...\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("[WIFI] SUKCES! Uzyskano IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        
        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = BROKER_URL,
        };
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqtt_client);
    }
}

void wifi_mqtt_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    printf("[WIFI] Modul wlaczony, szukam sieci %s...\n", WIFI_SSID);
}

void mqtt_send_log(uint8_t user_id, const char* activity, const char* action) {
    if (!is_mqtt_connected || mqtt_client == NULL) return;
    
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"id\": %d, \"aktywnosc\": \"%s\", \"akcja\": \"%s\"}", 
             user_id, activity, action);
             
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, payload, 0, 0, 0);
    printf("[MQTT] Wyslano log do chmury: %s\n", payload);
}