#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LED_RED,
    LED_GREEN,
    LED_YELLOW,
    LED_COUNT
} led_t ;

esp_err_t indicators_init(void);
void led_set(led_t led, bool on);

esp_err_t buzzer_tone(uint32_t freq_hz);
void buzzer_off(void);