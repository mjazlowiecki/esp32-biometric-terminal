#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_config.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "indicators.h"
#include <stdbool.h>

static const char *TAG = "terminal";

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(indicators_init());

    ESP_ERROR_CHECK(encoder_init());

    while (1)
    {
        encoder_event_t ev;
        if (encoder_get_event(&ev, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Enkoder: %s", ev == ENC_EVENT_RIGHT ? "prawo" : "lewo");
        }
    }
}