#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "ds3231.h"
#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "indicators.h"
#include "oled.h"
#include <stdbool.h>
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
    u8g2_DrawStr(d, (w - u8g2_GetStrWidth(d, date_str)) / 2, 50, date_str);

    if (!valid)
    {
        u8g2_DrawStr(d, 0, 63, "Czas niepewny");
    }
    u8g2_SendBuffer(d);
}

static void clock_update(u8g2_t *d)
{
    static int s_last_sec = -1;

    struct tm t;
    bool valid;
    if (ds3231_read_time(&t, &valid) != ESP_OK)
    {
        return;
    }
    if (t.tm_sec == s_last_sec)
    {
        return;
    }
    s_last_sec = t.tm_sec;
    clock_draw(d, &t, valid);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(indicators_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    i2c_bus_scan();
    ESP_ERROR_CHECK(ds3231_init());
    ESP_ERROR_CHECK(oled_init());
    u8g2_t *d = oled_get();

    struct tm t;
    bool valid;
    ESP_ERROR_CHECK(ds3231_read_time(&t, &valid));
    if (!valid)
    {
        struct tm now = {
            .tm_year = 2026 - 1900,
            .tm_mon  = 10 - 1,
            .tm_mday = 30,
            .tm_hour = 16,
            .tm_min  = 30,
            .tm_sec  = 0,
        };
        ESP_ERROR_CHECK(ds3231_set_time(&now));
        ESP_LOGI(TAG, "RTC mial OSF - ustawiono czas");
    }

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
                    break;
                case ENC_EVENT_RIGHT:
                    ESP_LOGI(TAG, "prawo");
                    break;
                case ENC_EVENT_PRESS:
                    ESP_LOGI(TAG, "klik %d", xTaskGetTickCount());
                    break;
                case ENC_EVENT_LONG_PRESS:
                    ESP_LOGI(TAG, "dlugie wcisniecie");
                    break;
            }
        }
        if (xTaskGetTickCount() - last_poll >= poll_period)
        {
            last_poll = xTaskGetTickCount();
            clock_update(d);
        }
    }
}