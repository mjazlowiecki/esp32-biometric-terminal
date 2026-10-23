#include "esp_err.h"
#include "esp_log.h"
#include "indicators.h"
#include <stdbool.h>

static const char *TAG = "terminal";

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware v3 start");
    ESP_ERROR_CHECK(indicators_init());
    led_set(LED_GREEN, true);
    led_set(LED_RED, true);
    led_set(LED_YELLOW, true);
}
    