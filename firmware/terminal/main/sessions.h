#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct
{
    uint32_t start;       // chwila START, UTC time
    uint16_t activity_id; // 0 = nic nie trwa
    uint8_t flags;        // EVENT_FLAG_* z rekordu START (czyli czy czas byl pewny)
} session_t;

esp_err_t sessions_init(void);

// Trwajaca sesja uzytkownika albo NULL, gdy nic nie trwa.
const session_t *sessions_get(uint16_t user_id);

esp_err_t sessions_stop(uint16_t user_id);

//teraz tak, jesli cos TRWA to najpierw zapisac STOP a potem 
// START nastepnej aktywnosci, czyli zmiana aktywnosci
esp_err_t sessions_start(uint16_t user_id, uint16_t activity_id);