#pragma once
#include <stdbool.h>

#define PIN_BUZZER  GPIO_NUM_14
#define PIN_LED_R   GPIO_NUM_12
#define PIN_LED_G   GPIO_NUM_13
#define PIN_LED_Y   GPIO_NUM_4

void sygnalizacja_init(void);

void led_green(bool state);
void led_yellow(bool state);
void led_red(bool state);

void buzzer_beep_success(void);
void buzzer_beep_error(void);