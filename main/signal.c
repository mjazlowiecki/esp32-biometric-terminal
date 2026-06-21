#include "signal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// configure LEDC for buzzer
#define BUZZER_TIMER       LEDC_TIMER_0
#define BUZZER_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_DUTY_RES    LEDC_TIMER_13_BIT // 0-8191
#define BUZZER_DUTY        4095              // 50% = full volume

void sygnalizacja_init(void) {
    
    //led as output
    gpio_set_direction(PIN_LED_R, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_LED_G, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_LED_Y, GPIO_MODE_OUTPUT);

    // rturn off all leds 
    led_red(false);
    led_green(false);
    led_yellow(false);

    // hardware pwm buzzer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = BUZZER_MODE,
        .timer_num        = BUZZER_TIMER,
        .duty_resolution  = BUZZER_DUTY_RES,
        .freq_hz          = 2000, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = BUZZER_MODE,
        .channel        = BUZZER_CHANNEL,
        .timer_sel      = BUZZER_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_BUZZER,
        .duty           = 0, // quiet on start
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
}

// LED controll
void led_green(bool state)  { gpio_set_level(PIN_LED_G, state ? 1 : 0); }
void led_yellow(bool state) { gpio_set_level(PIN_LED_Y, state ? 1 : 0); }
void led_red(bool state)    { gpio_set_level(PIN_LED_R, state ? 1 : 0); }

// private helper function to play tone
static void play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

//ready-to-play sequences
void buzzer_beep_success(void) {
    play_tone(2000, 100); //short, hi tone
    vTaskDelay(pdMS_TO_TICKS(50));
    play_tone(2500, 150); // even higher tone
}

void buzzer_beep_error(void) {
    play_tone(300, 500); // low error tone
}