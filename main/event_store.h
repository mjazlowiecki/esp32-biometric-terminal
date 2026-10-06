#pragma once

/*
 * event_store - trwala kolejka zdarzen terminala
 *
 * Kazde zdarzenie dostaje unikalny , rosnacy numer 'seq' i trafia do pliku
 * events.csv (czyli: jedna linia = jeden rekord + CRC16) - wiec urwany zapis po np. zaniku
 * zasilania jest wykrywany i pomijany. Osobny plik acked.log przechowuje wskaznik do
 * tego co serwer juz "potwierdzil". Dzieki temu:
 *      - nic nie ginie przy braku sieci (rekord czeka w pliku)
 *      - po odzyskaniu sieci wysylamy zalegle rekordy od ostatniego ACK,
 *      - dostarczanie jest wykonywane przynajmniej raz, serwer odrzuca duplikaty po (dev, seq).
 * 
 * Wszystkie funkcje sa wielowatkowe - wykorzystuja mutex - append wola zadanie glowne,
 * peek/ack - zadanie synchronizacji.
 * 
 * TEMP: nazwy plikow w 8.3, do czasu LFN we FATFS.
 */

#include <stdint.h>
#include <stdbool.h>

#define ES_ACTIVITY_LEN 20 /* = MAX_ACTIVITY_NAME_LEN */
#define ES_ACTION_LEN 8

typedef struct 
{
    uint32_t seq;         /* Unikalny numer zdarzenia w obrebie terminala */
    uint32_t boot_id;     /* Numer uruchomienia jako licznik w NVS */
    uint32_t mono_s;      /* Sekundy od startu systemu */
    uint32_t epoch;       /* Czas UTC (unix); 0 -> czas nieznany */
    bool epoch_estimated; /* Czas odtworzony po fakcie z mono_s */
    uint8_t user_id;
    char activity[ES_ACTIVITY_LEN];
    char action[ES_ACTION_LEN]; /* "START / "STOP */
    uint32_t duration_s;        /* tylko dla STOP */
} es_event_t;

/* po zamontowaniu SD po nvs_flash_init() */ //ref. wifi_mqtt_init
bool event_store_init(void);

//bool event_store();

//dopisz zdarzenie do SD /* true, jesli rekord trwale zapisany */
bool event_store_append(uint8_t user_id, const char *activity, const char *action,
                        uint32_t duration_s, uint32_t *seq_out);

//zwrocic niepotwierdzony event
bool event_store_peek_unsent(es_event_t *out);

//acknowledg eventu
bool event_store_ack(uint32_t seq);

//przyblizona liczba niepotwierdzonych rekordow
uint32_t event_store_pending(void);

#ifndef ESP_PLATFORM

//dla testow
extern uint32_t es_test_epoch;  /* "0" -> czas niezsynchronizowany */
extern uint32_t es_test_mono;
void es_test_reset_nvs(void);
#endif
