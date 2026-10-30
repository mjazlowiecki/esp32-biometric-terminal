#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "ds3231.h"
#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_config.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "indicators.h"
#include <stdbool.h>

static const char *TAG = "terminal";

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(indicators_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    i2c_bus_scan();
    ESP_ERROR_CHECK(ds3231_init());

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

    for (int i = 0; i < 2; i++)
    {
        struct tm t;
        bool valid;

        if (ds3231_read_time(&t, &valid) == ESP_OK)
        {
            ESP_LOGI(TAG, "RTC: %04d-%02d-%02d %02d:%02d:%02d %s", t.tm_year + 1900, t.tm_mon + 1,
                     t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                     valid ? "(OK)" : "(OSF: czas niewiarygodny)");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    ESP_ERROR_CHECK(encoder_init());

    while (1)
    {
        encoder_event_t ev;
        if (encoder_get_event(&ev, portMAX_DELAY))
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
                    ESP_LOGI(TAG, "klik");
                    break;
                case ENC_EVENT_LONG_PRESS:
                    ESP_LOGI(TAG, "dlugie wcisniecie");
                    break;
            }
        }
    }
}