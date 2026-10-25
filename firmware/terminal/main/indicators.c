#include "indicators.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "indicators";

static const gpio_num_t led_pins[LED_COUNT] = {
    [LED_RED]    = GPIO_NUM_12,
    [LED_GREEN]  = GPIO_NUM_13,
    [LED_YELLOW] = GPIO_NUM_4,
};

#define LED_FLASH_MS 1000

#define BUZZER_GPIO      GPIO_NUM_14
#define BUZZER_MODE      LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER     LEDC_TIMER_0
#define BUZZER_CHANNEL   LEDC_CHANNEL_0
#define BUZZER_DUTY_RES  LEDC_TIMER_10_BIT
#define BUZZER_DUTY_HALF (1 << 9)
#define BUZZER_INIT_HZ   2000

#define TONE_SUCCESS_HZ 2700
#define TONE_SUCCESS_MS 120
#define TONE_ERROR_HZ   500
#define TONE_ERROR_MS   600

static esp_timer_handle_t s_buzzer_timer;
static esp_timer_handle_t s_led_timer;
static led_t s_flash_led = LED_COUNT; // LED_COUNT = zadna dioda nie blyska

static void buzzer_timer_cb(void *arg)
{
    (void)arg;
    buzzer_off();
}

static esp_err_t buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = BUZZER_MODE,
        .timer_num       = BUZZER_TIMER,
        .duty_resolution = BUZZER_DUTY_RES,
        .freq_hz         = BUZZER_INIT_HZ,
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

    err = ledc_channel_config(&channel);
    if (err != ESP_OK)
    {
        return err;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = buzzer_timer_cb,
        .name     = "buzzer",
    };

    return esp_timer_create(&timer_args, &s_buzzer_timer);
}

static void led_timer_cb(void *arg)
{
    (void)arg;
    led_set(s_flash_led, false);
    s_flash_led = LED_COUNT;
}

static void led_flash(led_t led, uint32_t duration_ms)
{
    (void)esp_timer_stop(s_led_timer);
    led_set(s_flash_led, false);

    s_flash_led = led;
    led_set(led, true);
    (void)esp_timer_start_once(s_led_timer, (uint64_t)duration_ms * 1000);
}

static void indicate(led_t led, uint32_t freq_hz, uint32_t beep_ms)
{
    led_flash(led, LED_FLASH_MS);
    if (buzzer_beep(freq_hz, beep_ms) != ESP_OK)
    {
        ESP_LOGW(TAG, "Nie udalo sie zagrac sygnalu %lu Hz", freq_hz);
    }
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

    err = buzzer_init();
    if (err != ESP_OK)
    {
        return err;
    }

    const esp_timer_create_args_t led_timer_args = {
        .callback = led_timer_cb,
        .name     = "led_flash",
    };

    return esp_timer_create(&led_timer_args, &s_led_timer);
}

void led_set(led_t led, bool on)
{
    if (led >= LED_COUNT)
    {
        return;
    }
    gpio_set_level(led_pins[led], on ? 1 : 0);
}

esp_err_t buzzer_tone(uint32_t freq_hz)
{
    esp_err_t err = ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
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

esp_err_t buzzer_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    (void)esp_timer_stop(s_buzzer_timer);

    esp_err_t err = buzzer_tone(freq_hz);
    if (err != ESP_OK)
    {
        return err;
    }
    return esp_timer_start_once(s_buzzer_timer, (uint64_t)duration_ms * 1000);
}

void indicate_success(void)
{
    indicate(LED_GREEN, TONE_SUCCESS_HZ, TONE_SUCCESS_MS);
}

void indicate_error(void)
{
    indicate(LED_RED, TONE_ERROR_HZ, TONE_ERROR_MS);
}