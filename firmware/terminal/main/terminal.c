#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/projdefs.h"
#include "indicators.h"
#include <stdbool.h>

static const char *TAG = "terminal";

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(indicators_init());
    led_set(LED_GREEN, true);

    ESP_ERROR_CHECK(buzzer_beep(2000,200));
    ESP_LOGI(TAG, "buzzer dziala, app_main kontynuuje prace");

}    