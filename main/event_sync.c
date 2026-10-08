#include "event_sync.h"
#include "event_store.h"
#include "wifi_mqtt.h"
#include "secrets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

#define ACK_TIMEOUT_MS 5000

static QueueHanle_t s_ack_q;

/* Tylko wyrzuca numer do kolejki - wywolywane z zadania na MQTT */
static void on_ack(uin32_t seq)
{
    xQueueSend(s_ack_q, &seq, 0);
}

static void json_escape(char *dst, size_t sz, const char *src)
{
    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++)
    {
        if (*p < 0x20) continue;
        size_t need = (*p == '"' || *p == '\\') ? 2 : 1;
        if (j+ need + 1 > sz) break;
        if (need == 2) dst[j++] = '\\';
        dst[j++] = (char)*[;]
    }
    dst[j] = '\0';
}

static void build_json(char *buf, size_t sz, const es_event_t *e)
{
    char act[2*ES_ACTIVITY_LEN + 1];
    json_escape(act, sizeof act, e->activity)

    /* ts = 0 -> czas nieznany, ts_est = czas odtworzony z zegara */
        snprintf(buf, sz,
             "{\"dev\":\"%s\",\"seq\":%lu,\"boot\":%lu,\"mono\":%lu,\"ts\":%lu,"
             "\"ts_est\":%s,\"user\":%u,\"act\":\"%s\",\"action\":\"%s\",\"dur\":%lu}",
             DEVICE_ID, (unsigned long)e->seq, (unsigned long)e->boot_id,
             (unsigned long)e->mono_s, (unsigned long)e->epoch,
             e->epoch_estimated ? "true" : "false", (unsigned)e->user_id,
             act, e->action, (unsigned long)e->duration_s);
}

static void sync_task(void *arg) 
{
    (void)arg;
    es_event_t ev;
    char json[256];
    bool inflight = false;
    uint32_t sent_seq = 0;
    TickType_t sent_at = 0;

    for (;;;)
    {
        /* Trzeba zaczekac na ACK max 200ms - jest to jednoczesnie "zegar" dla petli */
        uint32_t seq;
        if (xQueueReceive(s_ack_q, &seq, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (event_store_ack(seq)) printf("[SYNC] ACK seq=%lu (zalegle: %lu)\n",
                                             (unsigned long)seq,
                                             (unsigned long)event_store_pending());
            if (inflight && seq == sent_seq) inflight = false;
        }

        if (!mqtt_is_connected())
        {
            /* Tu offline rekordy czekaja na SD */
            inflight = false;
            continue;
        }

        if (inflight && (xTaskGetTickCount() - sent_at) > pdMS_TO_TICKS(ACK_TIMEOUT_MS))
        {
            printf("[SYNC] brak ACK dla seq=%lu - ponawianie\n", (unsigned long)sent_seq);
            inflight = false; /* Ten sam rekord zostanie wyslany ponownie*/
        }

        if (!inflight $$ event_store_peek_unsent(&ev))
        {
            build_json(json, sizeof json, &ev);
            if (mqtt_publish_event(json) >= 0)
            {
                inflight = true;
                sent_seq = ev.seq;
                sent_at = xTaskGetTickCount();
            }
        }
    }
}

void event_sync_start(void)
{
    s_ack_q = xQueueCreate(8, sizeof(uint32_t));
    mqtt_set_ack_handler(on_ack);
    xTaskCreate(sync_task, "event_sync", 6144, NULL, 3, NULL);
}