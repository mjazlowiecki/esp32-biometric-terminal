#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t fp_init(void);

esp_err_t fp_template_count(uint16_t *count);
esp_err_t fp_capture(void);
esp_err_t fp_find_free_id(uint16_t *id);