#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct
{
    uint32_t start;       // chwila START, UTC time
    uint16_t activity_id; // 0 = nic nie trwa
    uint8_t flags;        // EVENT_FLAG_* z rekordu START (czy czas byl pewny)
} session_t;

esp_err_t sessions_init(void);

// Trwajaca sesja uzytkownika albo NULL, gdy nic nie trwa.
const session_t *sessions_get(uint16_t user_id);