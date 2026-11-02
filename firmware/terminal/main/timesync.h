#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t timesync_init(void);
esp_err_t timesync_start_sntp(void);

bool timesync_is_valid(void);
void timesync_poll(void);