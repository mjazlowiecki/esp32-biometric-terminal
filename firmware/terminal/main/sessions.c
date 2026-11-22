#include "sessions.h"

#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "event_store.h"
#include "fingerprint.h"

static const char *TAG = "sessions";

static session_t s_sessions[FP_CAPACITY]; // indekx = user id | wyzerowana na starcie

// ostatni wpis wygrywa - jedyne miejsce ktore zmienia tablice
static void apply(const event_record_t *rec)
{
    if (rec->user_id >= FP_CAPACITY)
    {
        return; // nie indeksowac tablicy niesprawdzana liczba z pliku
    }

    session_t *s = &s_sessions[rec->user_id];
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
    else if (rec->action == EVENT_ACTION_ENROLL)
    {
        s->activity_id = 0; // nowa osoba pod tym numerem nie dziedziczy cudzej sesji
    }
}

static void rebuild_cb(const event_record_t *rec, void *ctx)
{
    (void)ctx;
    apply(rec);
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

esp_err_t sessions_stop(uint16_t user_id)
{
    ESP_RETURN_ON_FALSE(user_id < FP_CAPACITY, ESP_ERR_INVALID_ARG, TAG, "zle ID %u", user_id);
    uint16_t current = s_sessions[user_id].activity_id;
    ESP_RETURN_ON_FALSE(current != 0, ESP_ERR_INVALID_STATE, TAG, "ID %u: nic nie trwa", user_id);

    event_record_t rec;
    ESP_RETURN_ON_ERROR(event_store_append(user_id, current, EVENT_ACTION_STOP, &rec), TAG,
                        "zapis STOP");
    apply(&rec); // do RAM dopiero PO udanym zapisie na karte

    ESP_LOGI(TAG, "STOP  #%lu: ID %u, akt %u", (unsigned long)rec.seq, user_id, current);
    return ESP_OK;
}

esp_err_t sessions_start(uint16_t user_id, uint16_t activity_id)
{
    ESP_RETURN_ON_FALSE(user_id < FP_CAPACITY, ESP_ERR_INVALID_ARG, TAG, "zle ID %u", user_id);
    ESP_RETURN_ON_FALSE(activity_id != 0, ESP_ERR_INVALID_ARG, TAG, "aktywnosc 0 zarezerwowana");

    if (s_sessions[user_id].activity_id != 0)
    {
        ESP_RETURN_ON_ERROR(sessions_stop(user_id), TAG, "konczenie poprzedniej");
    }

    event_record_t rec;
    ESP_RETURN_ON_ERROR(event_store_append(user_id, activity_id, EVENT_ACTION_START, &rec), TAG,
                        "zapis START");
    apply(&rec);

    ESP_LOGI(TAG, "START #%lu: ID %u, akt %u", (unsigned long)rec.seq, user_id, activity_id);
    return ESP_OK;
}

esp_err_t sessions_enroll(uint16_t user_id)
{
    ESP_RETURN_ON_FALSE(user_id < FP_CAPACITY, ESP_ERR_INVALID_ARG, TAG, " zle ID %u", user_id);

    if (s_sessions[user_id].activity_id != 0)
    {
        ESP_LOGW(TAG, "ID %u mial otwarta sesje (akt %u) - zostaje porzucona", user_id,
                 s_sessions[user_id].activity_id);
    }

    event_record_t rec;
    ESP_RETURN_ON_ERROR(event_store_append(user_id, 0, EVENT_ACTION_ENROLL, &rec), TAG,
                        "zapis ENROLL");
    apply(&rec);

    ESP_LOGI(TAG, "ENROLL #%lu: ID %u", (unsigned long)rec.seq, user_id);
    return ESP_OK;
}