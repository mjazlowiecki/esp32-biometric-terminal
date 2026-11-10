#include "activities.h"

#include <stddef.h>

// TYMCZASOWO: stala lista. Docelowo konfiguracja z serwera (JSON w NVS).
static const activity_t s_activities[] = {
    {.id = 1, .name = "Praca", .min_min = ACTIVITY_NO_LIMIT, .max_min = ACTIVITY_NO_LIMIT},
    {.id = 2, .name = "Spotkanie", .min_min = ACTIVITY_NO_LIMIT, .max_min = 60},
    {.id = 3, .name = "Przerwa", .min_min = ACTIVITY_NO_LIMIT, .max_min = 15},
    {.id = 4, .name = "Nauka", .min_min = 45, .max_min = ACTIVITY_NO_LIMIT},
};

#define ACTIVITIES_N (sizeof(s_activities) / sizeof(s_activities[0]))

size_t activities_count(void)
{
    return ACTIVITIES_N;
}

const activity_t *activities_get(size_t index)
{
    return index < ACTIVITIES_N ? &s_activities[index] : NULL;
}

const activity_t *activities_find(uint16_t id)
{
    for (size_t i = 0; i < ACTIVITIES_N; i++)
    {
        if (s_activities[i].id == id)
        {
            return &s_activities[i];
        }
    }
    return NULL;
}