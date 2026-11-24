#pragma once

#include <stdbool.h>

#include "esp_err.h"

//jednokrotnie utworz klienta MQTT  + laczenie w tle
//biblioteka odpowiada za ponawianie prob

esp_err_t mqtt_link_init(void);
bool mqtt_link_is_connected(void);

esp_err_t mqtt_link_publish_event(const char *json); //retain  =0 

//subskrybent nie dostaje ostatniego zdarzenia jako aktualnego ^^