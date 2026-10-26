#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum 
{
    ENC_EVENT_LEFT,
    ENC_EVENT_RIGHT,
} encoder_event_t;

esp_err_t encoder_init(void);
bool encoder_get_event(encoder_event_t *event, TickType_t timeout);
//int encoder_get_count(void);