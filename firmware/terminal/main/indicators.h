#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    LED_RED,
    LED_GREEN,
    LED_YELLOW,
    LED_COUNT
} led_t ;

esp_err_t indicators_init(void);
void led_set(led_t led, bool on);