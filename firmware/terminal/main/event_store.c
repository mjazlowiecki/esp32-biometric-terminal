#include "event_store.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "sdcard.h"
#include "timesync.h"

static const char *TAG = "event_store";

#define EVENTS_PATH   SDCARD_MOUNT_POINT "/events.bin"
#define NVS_NAMESPACE "evstore"
#define NVS_KEY_NEXT  "next_seq"
#define NVS_KEY_ACKED "acked_seq"

static nvs_handle_t s_nvs;
static uint32_t s_next_seq;     // numer, ktory dostanie NASTEPNE zdarzenie
static uint32_t s_acked_seq;    // zakladka: potwierdzone wszystko do tego numeru
static uint32_t s_record_count; // ile pelnych rekordow jest w pliku
static bool s_ready;            // true dopiero po udanym event_store_init

uint32_t event_record_crc(const event_record_t *rec)
{
    return esp_rom_crc32_le(0, (const uint8_t *)rec, offsetof(event_record_t, crc));
}

static esp_err_t nvs_get_u32_default(const char *key, uint32_t def, uint32_t *out)
{
    esp_err_t err = nvs_get_u32(s_nvs, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        *out = def;
        return ESP_OK;
    }
    return err;
}

esp_err_t event_store_init(void)
{
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs), TAG, "nvs_open");
    ESP_RETURN_ON_ERROR(nvs_get_u32_default(NVS_KEY_NEXT, 1, &s_next_seq), TAG, "next_seq");
    ESP_RETURN_ON_ERROR(nvs_get_u32_default(NVS_KEY_ACKED, 0, &s_acked_seq), TAG, "acked_seq");

    struct stat st;
    if (stat(EVENTS_PATH, &st) != 0)
    {
        FILE *f = fopen(EVENTS_PATH, "ab");
        ESP_RETURN_ON_FALSE(f != NULL, ESP_FAIL, TAG, "nie mozna utworzyc %s", EVENTS_PATH);
        fclose(f);
        st.st_size = 0;
        ESP_LOGI(TAG, "Utworzono pusty dziennik %s", EVENTS_PATH);
    }

    s_record_count = st.st_size / sizeof(event_record_t);
    uint32_t tail  = st.st_size % sizeof(event_record_t);

    ESP_LOGI(TAG, "Dziennik: %lu rekordow, next_seq=%lu, acked_seq=%lu",
             (unsigned long)s_record_count, (unsigned long)s_next_seq, (unsigned long)s_acked_seq);
    if (tail != 0)
    {
        ESP_LOGW(TAG, "Na koncu pliku %lu B niepelnego rekordu", (unsigned long)tail);
    }

    s_ready = true;
    return ESP_OK;
}

esp_err_t event_store_append(uint16_t user_id, uint16_t activity_id, event_action_t action,
                             uint32_t *out_seq)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "magazyn niezainicjowany");

    // Rezerwacja numeru: NAJPIERW trwale zapisz kolejny licznik.
    // Zanik zasilania po tym kroku = dziura w numeracji,
    // nigdy dwa rozne zdarzenia z tym samym numerem.
    uint32_t seq = s_next_seq;
    ESP_RETURN_ON_ERROR(nvs_set_u32(s_nvs, NVS_KEY_NEXT, seq + 1), TAG, "nvs_set");
    ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "nvs_commit");
    s_next_seq = seq + 1;

    // rekord
    event_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.seq         = seq;
    rec.timestamp   = (uint32_t)time(NULL);
    rec.user_id     = user_id;
    rec.activity_id = activity_id;
    rec.action      = (uint8_t)action;
    rec.flags       = timesync_is_valid() ? EVENT_FLAG_TIME_SYNCED : 0;
    rec.crc         = event_record_crc(&rec);

    // dopisz i wymus fizyczny zpais na karte SD
    FILE *f = fopen(EVENTS_PATH, "ab");
    ESP_RETURN_ON_FALSE(f != NULL, ESP_FAIL, TAG, "fopen");
    bool ok = fwrite(&rec, sizeof(rec), 1, f) == 1 && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok      = (fclose(f) == 0) && ok;
    ESP_RETURN_ON_FALSE(ok, ESP_FAIL, TAG, "zapis rekordu #%lu", (unsigned long)seq);

    s_record_count++;
    if (out_seq != NULL)
    {
        *out_seq = seq;
    }
    return ESP_OK;
}