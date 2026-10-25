#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LED_RED,
    LED_GREEN,
    LED_YELLOW,
    LED_COUNT
} led_t ;

/**
 * @brief Inicjalizacja sygnalizacji wizualo-dzwiekowej urzadzenia.
 *
 * Odpowiada za inicjalizacje oraz konfiguracje GPIO dla diod LED.
 * Dodatkwo, tworzy oraz konfiguruje timer dla diod.
 *
 * @return ESP_OK albo kod bledu LEDC / esp_timer
 */
esp_err_t indicators_init(void);

/**
 * @brief Inicjalizacja sygnalizacji wizualo-dzwiekowej urzadzenia.
 *
 * Ustawia czestotliwosc dzwieku oraz
 * odpowiada za wypelnienie i aktualizacje cyklu pracy buzzera.
 *
 * @param freq_hz     czestotliwosc tonu [Hz]
 * @return            ESP_OK albo kod bledu LEDC / esp_timer
 */
esp_err_t buzzer_tone(uint32_t freq_hz);

/**
 * @brief Wylacza buzzer.
 *
 */
void buzzer_off(void);

/**
 * @brief Gra ton przez zadany czas, bez blokowania.
 *
 * Wraca natychmiast, dzwiek wylacza esp_timer.
 * Nowe wywolanie przerywa trwajace pikniecie.
 *
 * @param freq_hz     czestotliwosc tonu [Hz]
 * @param duration_ms czas trwania [ms]
 * @return ESP_OK albo kod bledu LEDC / esp_timer
 */
esp_err_t buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief Zaswieca odpowiednie diody LED.
 *
 * W wypadku podania nieprawidlowej wartosci argumentu led_t -
 * funckja wraca natychmiast
 *
 * @param led     odpowiednia dioda do zaswiecenia
 * @param on      wartosc logiczna true = wlaczona / false = wylaczona
 * @return ESP_OK albo kod bledu LEDC / esp_timer
 */
void led_set(led_t led, bool on);

/**
 * @brief Sygnal sukcesu: zielona dioda na 1 s + krotki wysoki ton.
 *
 * Best effort - blad sygnalizacji jest tylko logowany,
 * nie przerywa dzialania wywolujacego.
 */
void indicate_success(void);

/**
 * @brief Sygnal bledu: czerwona dioda na 1 s + krotki niski ton.
 *
 * Best effort - blad sygnalizacji jest tylko logowany,
 * nie przerywa dzialania wywolujacego.
 */
void indicate_error(void);