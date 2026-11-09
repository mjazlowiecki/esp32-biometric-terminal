#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum 
{
    EVENT_ACTION_START = 1,
    EVENT_ACTION_STOP = 2,
} event_action_t;

#define EVENT_FLAG_TIME_SYNCED 0x01

// Rekord zapisywany 1:1 do /sd/events.bin. Ulozenie pol bez dziur:
// kazde pole lezy pod adresem podzielnym przez swoj rozmiar.
typedef struct
{
    uint32_t seq; // numer kolejny zdarzenia - nigdy sie nie powtarza
    uint32_t timestamp; // sekundy od 1970-01-01, UTC
    uint16_t user_id; //ID szablonu w czytniku R307s
    uint16_t activity_id; // ID aktywnosci z konfiguracji serwera
    uint8_t action; // event_action_t
    uint8_t flags; // EVENT_FLAG_*
    uint8_t reserved[2]; // zarezerwowane zerami zamiast ukrytego paddingu
    uint32_t crc; //CRC32 wszystkich pol powyzej
} event_record_t;

uint32_t event_record_crc(const event_record_t *rec);
esp_err_t event_store_init(void);
esp_err_t event_store_append(uint16_t user_id, uint16_t activity_id, event_action_t action, uint32_t *out_seq);

esp_err_t event_store_peek(event_record_t *out);
esp_err_t event_store_ack(uint32_t seq);

_Static_assert(sizeof(event_record_t) == 20, "zmienil sie rozmiar rekordu - format pliku!");

_Static_assert(offsetof(event_record_t, crc) == offsetof(event_record_t, reserved) + sizeof(((event_record_t *)0)->reserved), "ukryty padding przed polem crc!");