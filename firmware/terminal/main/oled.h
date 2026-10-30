#pragma once

#include "esp_err.h"
#include "u8g2.h"

esp_err_t oled_init(void);
u8g2_t *oled_get(void); //oddaj wskaznik na obiet u8g2