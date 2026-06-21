#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#define PIN_MISO  GPIO_NUM_19
#define PIN_MOSI  GPIO_NUM_23
#define PIN_CLK   GPIO_NUM_18
#define PIN_CS    GPIO_NUM_5

void sdcard_init(void);
void sdcard_log_activity(uint8_t user_id, const char* activity_name, const char* action);