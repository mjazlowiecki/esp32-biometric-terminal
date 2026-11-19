#include "sessions.h"

#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "event_store.h"
#include "fingerprint.h"

static const char *TAG = "sessions";

static session_t s_sessions[FP_CAPACITY]; // indekx = user id | wyzerowana na starcie

static void rebuild_cb(const event_record_t *rec, void *ctx)
{
    (void)ctx; // nie uzywamy kontekstu, bo tablica jest w module

    if (rec->user_id >= FP_CAPACITY)
    {
        return; // nie powinno sie indeksowac tablicy niesprawdzona liczba z pliku
    }

    session_t *s = &s_sessions[rec->user_id]; // zeby nie pisac s_sessions[rec->user_id]
    if (rec->action == EVENT_ACTION_START)
    {
        s->activity_id = rec->activity_id;
        s->start       = rec->timestamp;
        s->flags       = rec->flags;
    }
    else if (rec->action == EVENT_ACTION_STOP)
    {
        s->activity_id = 0;
    }
}

esp_err_t sessions_init(void)
{
    ESP_RETURN_ON_ERROR(event_store_for_each(rebuild_cb, NULL), TAG, "odbudowa z dziennika");

    uint32_t active = 0;
    for (uint16_t id = 0; id < FP_CAPACITY; id++)
    {
        const session_t *s = &s_sessions[id];
        if (s->activity_id == 0)
        {
            continue;
        }
        active++;

        time_t t = s->start;
        struct tm tm;
        char when[20];
        localtime_r(&t, &tm);
        strftime(when, sizeof(when), "%d.%m.%Y %H:%M:%S", &tm);
        ESP_LOGI(TAG, "ID %u: akt %u od %s%s", id, s->activity_id, when,
                 (s->flags & EVENT_FLAG_TIME_SYNCED) ? "" : " (czas niepewny)");
    }
    ESP_LOGI(TAG, "Trwajacych sesji: %lu", (unsigned long)active);
    return ESP_OK;
}

const session_t *sessions_get(uint16_t user_id)
{
    if (user_id >= FP_CAPACITY || s_sessions[user_id].activity_id == 0)
    {
        return NULL;
    }
    return &s_sessions[user_id];
}
