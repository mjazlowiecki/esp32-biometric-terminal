#include "indicators.h"
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include <driver/gpio.h>
#include <driver/ledc.h>

static const gpio_num_t led_pins[LED_COUNT] = {
    [LED_RED]      =   GPIO_NUM_12,
    [LED_GREEN]    =   GPIO_NUM_13,
    [LED_YELLOW]   =   GPIO_NUM_4,
};

#define BUZZER_GPIO     GPIO_NUM_14
#define BUZZER_MODE     LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER    LEDC_TIMER_0
#define BUZZER_CHANNEL  LEDC_CHANNEL_0
#define BUZZER_DUTY_RES LEDC_TIMER_10_BIT
#define BUZZER_DUTY_HALF (1 << 9)

static esp_err_t buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = BUZZER_MODE,
        .timer_num       = BUZZER_TIMER,
        .duty_resolution = BUZZER_DUTY_RES,
        .freq_hz         = 2000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK)
    {
        return err;
    }

    ledc_channel_config_t channel = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel    = BUZZER_CHANNEL,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&channel);
}

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

    return buzzer_init();
}

void led_set(led_t led, bool on)
{
    if (led >= LED_COUNT)
    {
        return;
    }
    gpio_set_level(led_pins[led], on ? 1: 0);
}

esp_err_t buzzer_tone(uint32_t freq_hz)
{
    esp_err_t err = ledc_set_freq(BUZZER_MODE,BUZZER_TIMER, freq_hz);
    if (err != ESP_OK)
    {
        return err;
    }
    
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, BUZZER_DUTY_HALF);
    return ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

void buzzer_off(void)
{
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}