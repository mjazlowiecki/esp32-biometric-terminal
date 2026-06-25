#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_ACTIVITIES 6
#define MAX_ACTIVITY_NAME_LEN 20

// Zmienne do komunikatow z chmury
extern volatile bool new_cloud_cmd;
extern char cloud_cmd_buf[64];

// Dynamiczna lista aktywnosci kontrolowana z Node-RED
extern char dynamic_activities[MAX_ACTIVITIES][MAX_ACTIVITY_NAME_LEN];
extern int dynamic_activities_count;

// Zainicjuje WiFi i polaczy sie z darmowym publicznym brokerem
void wifi_mqtt_init(void);

// Funkcja do wysylania logow
void mqtt_send_log(uint8_t user_id, const char* activity, const char* action);