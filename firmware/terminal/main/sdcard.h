#pragma once

#include "esp_err.h"

#define SDCARD_MOUNT_POINT "/sd"

esp_err_t sdcard_init(void);