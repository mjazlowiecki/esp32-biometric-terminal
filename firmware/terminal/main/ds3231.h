#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

esp_err_t ds3231_init(void);
esp_err_t ds3231_read_time(struct tm *out, bool *valid);