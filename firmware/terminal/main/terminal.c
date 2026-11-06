#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "ds3231.h"
#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "event_store.h"
#include "fingerprint.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "indicators.h"
#include "net.h"
#include "nvs_flash.h"
#include "oled.h"
#include "sdcard.h"
#include "string.h"
#include "timesync.h"
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "terminal";

#define CLOCK_POLL_MS 250

static void clock_draw(u8g2_t *d, const struct tm *t, bool valid)
{
    char time_str[9];  // "HH:MM:SS" + '\0'
    char date_str[11]; // "DD.MM.YYYY" + '\0'
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);
    strftime(date_str, sizeof(date_str), "%d.%m.%Y", t);

    int w = u8g2_GetDisplayWidth(d);
    u8g2_ClearBuffer(d);

    u8g2_SetFont(d, u8g2_font_logisoso24_tn);
    u8g2_DrawStr(d, (w - u8g2_GetStrWidth(d, time_str)) / 2, 34, time_str);

    u8g2_SetFont(d, u8g2_font_6x10_tr);
    u8g2_DrawStr(d, 0, 8, net_is_connected() ? "WiFi" : "offline");
    u8g2_DrawStr(d, (w - u8g2_GetStrWidth(d, date_str)) / 2, 50, date_str);

    if (!valid)
    {
        u8g2_DrawStr(d, 0, 63, "Czas niepewny");
    }
    u8g2_SendBuffer(d);
}

static void clock_update(u8g2_t *d)
{
    static int s_last = -1;

    time_t now = time(NULL);
    if (now == s_last)
    {
        return;
    }
    s_last = now;
    struct tm local;
    localtime_r(&now, &local);
    clock_draw(d, &local, timesync_is_valid());
}

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(indicators_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    i2c_bus_scan();
    ESP_ERROR_CHECK(ds3231_init());
    ESP_ERROR_CHECK(timesync_init());

    if (sdcard_init() == ESP_OK)
    {
        if (event_store_init() != ESP_OK)
        {
            ESP_LOGE(TAG, "Magazyn zdarzen niedostepny!");
            indicate_error();
        }
    }
    else
    {
        ESP_LOGE(TAG, "Brak karty SD - zdarzenia nie beda zapisywane!");
        indicate_error();
    }

    if (fp_init() == ESP_OK)
    {
        uint16_t templates;
        if (fp_template_count(&templates) == ESP_OK)
        {
            ESP_LOGI(TAG, "Czytnik OK, szablonow w pamieci: %u", templates);
        }
        else
        {
            indicate_error();
        }
    }
    else
    {
        ESP_LOGE(TAG, "Czytnik linii papilarnych niedostepny!");
        indicate_error();
    }

    ESP_ERROR_CHECK(oled_init());

    if (net_init() == ESP_OK)
    {
        ESP_ERROR_CHECK(timesync_start_sntp());
    }
    else
    {
        ESP_LOGW(TAG, "Siec niedostepna - praca offline");
    }

    u8g2_t *d = oled_get();

    ESP_ERROR_CHECK(encoder_init());

    TickType_t poll_period = pdMS_TO_TICKS(CLOCK_POLL_MS);
    TickType_t last_poll   = xTaskGetTickCount();
    clock_update(d);

    while (1)
    {
        TickType_t elapsed = xTaskGetTickCount() - last_poll;
        TickType_t wait    = (elapsed < poll_period) ? poll_period - elapsed : 0;

        encoder_event_t ev;
        if (encoder_get_event(&ev, wait))
        {
            switch (ev)
            {
                case ENC_EVENT_LEFT:
                    ESP_LOGI(TAG, "lewo");
                    led_set(LED_GREEN, true);
                    led_set(LED_RED, false);
                    break;
                case ENC_EVENT_RIGHT:
                {
                    event_record_t rec;
                    if (event_store_peek(&rec) == ESP_OK)
                    {
                        ESP_LOGI(TAG, "Do wyslania: #%lu", (unsigned long)rec.seq);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Nic do wyslania");
                    }
                    break;
                }
                case ENC_EVENT_PRESS:
                {
                    uint32_t seq;
                    int64_t t0    = esp_timer_get_time();
                    esp_err_t err = event_store_append(1, 1, EVENT_ACTION_START, &seq);
                    int64_t us    = esp_timer_get_time() - t0;
                    if (err == ESP_OK)
                    {
                        ESP_LOGI(TAG, "Zapisano zdarzenie #%lu w %lld ms", (unsigned long)seq,
                                 us / 1000);
                        indicate_success();
                    }
                    else
                    {
                        indicate_error();
                    }
                    break;
                }
                case ENC_EVENT_LONG_PRESS:
                {
                    event_record_t rec;
                    if (event_store_peek(&rec) == ESP_OK && event_store_ack(rec.seq) == ESP_OK)
                    {
                        ESP_LOGI(TAG, "ACK #%lu - zakladka przesunieta", (unsigned long)rec.seq);
                    }
                    break;
                }
            }
        }
        if (xTaskGetTickCount() - last_poll >= poll_period)
        {
            last_poll = xTaskGetTickCount();
            timesync_poll();
            clock_update(d);
        }
    }
}