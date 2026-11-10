#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "activities.h"
#include "ds3231.h"
#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
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
#include "timesync.h"
#include "ui.h"

#include <stdbool.h>

static const char *TAG = "terminal";

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
            uint16_t free_id;
            if (fp_find_free_id(&free_id) == ESP_OK)
            {
                ESP_LOGI(TAG, "Pierwsze wolne miejsce w bazie: %u", free_id);
            }
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

    ESP_ERROR_CHECK(encoder_init());
    ui_init(oled_get());

    TickType_t period    = pdMS_TO_TICKS(UI_TICK_MS);
    TickType_t last_tick = xTaskGetTickCount();

    while (1)
    {
        TickType_t elapsed = xTaskGetTickCount() - last_tick;
        TickType_t wait    = (elapsed < period) ? period - elapsed : 0;

        encoder_event_t ev;
        if (encoder_get_event(&ev, wait))
        {
            ui_on_encoder(ev);
        }

        if (xTaskGetTickCount() - last_tick >= period)
        {
            last_tick = xTaskGetTickCount();
            timesync_poll();
            ui_tick();
        }
    }
}