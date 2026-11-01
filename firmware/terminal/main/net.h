#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t net_init(void);
bool net_is_connected(void);
