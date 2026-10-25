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

    ESP_ERROR_CHECK(buzzer_tone(2000));
    vTaskDelay(pdMS_TO_TICKS(200));
    buzzer_off();
}    