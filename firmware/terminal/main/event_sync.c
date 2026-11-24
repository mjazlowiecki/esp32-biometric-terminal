#include "event_sync.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "event_store.h"
#include "mqtt_link.h"

static const char *TAG = "sync";

#define SYNC_ACK_TIMEOUT_MS 10000

static bool s_in_flight; // cos wyslane - oczekuj ACK
static uint32_t s_flight_seq;
static int64_t s_sent_us;

// pola z ktorych serwer odtworzy CRC
static void format_event(const event_record_t *r, char *buf, size_t len)
{
    snprintf(buf, len,
             "{\"seq\":%lu,\"ts\":%lu,\"user\":%u,\"act\":%u,\"action\":%u,\"flags\":%u,"
             "\"crc\":%lu}",
             (unsigned long)r->seq, (unsigned long)r->timestamp, r->user_id, r->activity_id,
             r->action, r->flags, (unsigned long)r->crc);
}

void event_sync_poll(void)
{
    if (!mqtt_link_is_connected() || !event_store_is_ready())
    {
        // wyslij ponownie od razu
        s_in_flight = false; // po zerwaniu pol. nic nie jest pewne
        return;
    }

    int64_t now = esp_timer_get_time();
    if (s_in_flight && now - s_sent_us < (int64_t)SYNC_ACK_TIMEOUT_MS * 1000)
    {
        // czekaj na ACK
        return;
    }

    event_record_t rec;
    if (event_store_peek(&rec) != ESP_OK)
    {
        s_in_flight = false; // nic do wyslania
        return;
    }

    if (s_in_flight)
    {
        ESP_LOGW(TAG, "Brak ACK dla #%lu po %d s - wysylam ponownie", (unsigned long)s_flight_seq,
                 SYNC_ACK_TIMEOUT_MS / 1000);
    }

    char json[160];
    format_event(&rec, json, sizeof(json));
    if (mqtt_link_publish_event(json) == ESP_OK)
    {
        s_in_flight  = true;
        s_flight_seq = rec.seq;
        s_sent_us    = now;
        ESP_LOGI(TAG, "Wyslano #%lu", (unsigned long)rec.seq);
    }
}