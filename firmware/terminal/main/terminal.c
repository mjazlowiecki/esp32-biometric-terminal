#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "activities.h"
#include "ds3231.h"
#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_store.h"
#include "event_sync.h"
#include "fingerprint.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "indicators.h"
#include "mqtt_link.h"
#include "net.h"
#include "nvs_flash.h"
#include "oled.h"
#include "sdcard.h"
#include "sessions.h"
#include "timesync.h"
#include "ui.h"

#include <stdbool.h>

static const char *TAG = "terminal";

#if CONFIG_TERMINAL_TEST_EVENTS > 0
#define TEST_USER_ID 999 // ostatnie miejsce w czytniku [0-999]

// TEST N zdarzen przechodzacych przez pelen cykl: [ sessions -> event_store -> event_sync ]
static void test_generate_events(int n)
{
    if (!event_store_is_ready() || event_store_record_count() != 0)
    {
        ESP_LOGW(TAG, "TEST: dziennik niepusty - generator pominiety");
        return;
    }
    ESP_LOGW(TAG, "TEST: generuje %d zdarzen uzytkownika %u", n, TEST_USER_ID);

    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < n; i++)
    {
        esp_err_t err = (i % 2 == 0) ? sessions_start(TEST_USER_ID, 1 + (i / 2) % 4)
                                     : sessions_stop(TEST_USER_ID);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "TEST: przerwano na zdarzeniu %d", i);
            return;
        }
    }
    ESP_LOGW(TAG, "TEST: %d zdarzen zapisanych w %lld ms", n, (esp_timer_get_time() - t0) / 1000);
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(indicators_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    i2c_bus_scan();
    ESP_ERROR_CHECK(ds3231_init());
    ESP_ERROR_CHECK(timesync_init());

    if (sdcard_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Brak karty SD - zdarzenia nie beda zapisywane!");
        indicate_error();
    }
    else if (event_store_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Magazyn zdarzen niedostepny!");
        indicate_error();
    }
    else
    {
        sessions_init(); // blad zaloguje sam
#if CONFIG_TERMINAL_TEST_EVENTS > 0
        test_generate_events(CONFIG_TERMINAL_TEST_EVENTS);
#endif
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
        mqtt_link_init();
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
            event_sync_poll();
        }
    }
}