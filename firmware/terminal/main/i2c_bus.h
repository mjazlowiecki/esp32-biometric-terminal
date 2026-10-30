#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_get(void);
void i2c_bus_scan(void);
esp_err_t ds3231_set_time(const struct tm *t);