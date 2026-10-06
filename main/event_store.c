#include "event_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef EVENT_STORE_DIR
#define EVENT_STORE_DIR "/sdcard"
#endif
#define EVENTS_PATH EVENT_STORE_DIR "/events.csv"
#define ACKS_PATH   EVENT_STORE_DIR "/acked.log"

#define MIN_VALID_EPOCH 1704067200u /* min. data sycnchronizacji tj. 2024-01-01, wczesniej czas niezsynchronizowany */

#ifdef ESP_PLATFORM
 
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs.h"

SemaphoreHandle_t s_lock;
#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static void plat_init(void) 
{ 
    if(!s_lock) s_lock = xSemaphoreCreateMutex(); 
}

static uint32_t plat_mono_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

static uint32_t plat_now_epoch(void) 
{
    time_t t = time(NULL);
    return (t >= (time_t)MIN_VALID_EPOCH ? (uint32_t)t : 0);
}

static uint32_t plat_u32_get(const char *key, uint32_t def)
{
    nvs_handle_t h;
    uint32_t v = def, tmp;

    if (nvs_open("evstore", NVS_READONLY, &h) == ESP_OK)
    {
        if (nvs_get_u32(h, key, &tmp) == ESP_OK) v = tmp;
        nvs_close(h);
    }
    return v;
}

static void plat_u32_set(const char *key, uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open("evstore", NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_u32(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

#else       /* HOST - TODO */
