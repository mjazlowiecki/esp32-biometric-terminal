#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h" // Biblioteka do sprzętowego PWM w ESP-IDF

// Zmień to na pin, do którego podłączyłeś buzzer na swoim PCB!
#define BUZZER_PIN GPIO_NUM_14 

// Konfiguracja kanału i timera LEDC
#define BUZZER_TIMER       LEDC_TIMER_0
#define BUZZER_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_DUTY_RES    LEDC_TIMER_13_BIT // Rozdzielczość 13-bitowa (0-8191)
#define BUZZER_DUTY        4095              // Wypełnienie 50% (dla maksymalnej głośności 4095 z 8191)
#define BUZZER_FREQ_HZ     2000              // Częstotliwość w Hz (2kHz - bardzo dobrze słyszalny pisk)

void buzzer_init() {
    // 1. Konfiguracja Timera
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = BUZZER_MODE,
        .timer_num        = BUZZER_TIMER,
        .duty_resolution  = BUZZER_DUTY_RES,
        .freq_hz          = BUZZER_FREQ_HZ,  // Ustawiamy podstawową częstotliwość
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Konfiguracja Kanału przypisanego do Timera i Pinu GPIO
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = BUZZER_MODE,
        .channel        = BUZZER_CHANNEL,
        .timer_sel      = BUZZER_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_PIN,
        .duty           = 0, // Na starcie buzzer jest wyciszony (wypełnienie 0%)
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
}

// Funkcja pomocnicza: wygrywa dźwięk przez zadaną liczbę milisekund z zadaną częstotliwością
void play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    // Zmiana częstotliwości
    ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    
    // Włączenie dźwięku (ustawienie wypełnienia na 50%)
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    
    // Czekamy (dźwięk trwa)
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // Wyłączenie dźwięku (wypełnienie 0%)
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

void app_main(void)
{
    printf("\n--- Test Buzzera ---\n");
    buzzer_init();

    while (1) {
        printf("Sygnal: Sukces (Krotkie, wesole dzwieki)\n");
        play_tone(2000, 100); // 2kHz przez 100ms
        vTaskDelay(pdMS_TO_TICKS(100)); // Przerwa 100ms
        play_tone(2500, 150); // 2.5kHz przez 150ms
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Czekamy 2 sekundy

        printf("Sygnal: Blad (Dlugi, niski dzwiek)\n");
        play_tone(400, 600); // 400Hz przez 600ms (buczenie)
        
        vTaskDelay(pdMS_TO_TICKS(3000)); // Czekamy 3 sekundy
    }
}