#pragma once

#include <stdint.h>
#include <stddef.h>

#define ACTIVITY_NO_LIMIT 0 // brak limitu (MIN albo MAX)

typedef struct
{
    uint16_t    id;      // numer zapisywany w rekordzie zdarzenia (activity_id)
    const char *name;    // nazwa na ekranie
    uint16_t    min_min; // minimalny czas w minutach (0 = brak)
    uint16_t    max_min; // maksymalny czas w minutach (0 = brak)
} activity_t;

size_t            activities_count(void);
const activity_t *activities_get(size_t index);
const activity_t *activities_find(uint16_t id);