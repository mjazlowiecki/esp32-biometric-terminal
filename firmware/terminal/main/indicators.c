#include "indicators.h"
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include <driver/gpio.h>

static const gpio_num_t led_pins[LED_COUNT] = {
    [LED_RED]      =   GPIO_NUM_12,
    [LED_GREEN]    =   GPIO_NUM_13,
    [LED_YELLOW]   =   GPIO_NUM_4,
};

esp_err_t indicators_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < LED_COUNT; i++)
    {
        mask |= 1ULL << led_pins[i];
    }

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
    {
        return err;
    }

    for (int i = 0; i < LED_COUNT; i++)
    {
        gpio_set_level(led_pins[i], 0);
    }
    return ESP_OK;
}

void led_set(led_t led, bool on)
{
    if (led >= LED_COUNT)
    {
        return;
    }
    gpio_set_level(led_pins[led], on ? 1: 0);
}