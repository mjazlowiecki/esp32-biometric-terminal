#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef enum 
{
    FP_STEP_PLACE_FIRST,  // przyloz palec (1. raz)
    FP_STEP_REMOVE,       // zabierz palec
    FP_STEP_PLACE_SECOND, // przyloz ten sam palec ponownie
} fp_enroll_step_t;

typedef void (*fp_enroll_cb_t)(fp_enroll_step_t step);

esp_err_t fp_init(void);

esp_err_t fp_template_count(uint16_t *count);
esp_err_t fp_capture(void);
esp_err_t fp_find_free_id(uint16_t *id);

// rej. nowego palca -zwraca:
//   ESP_OK                - zapisano, numer w *out_id
//   ESP_ERR_NOT_FOUND     - baza pelna
//   ESP_ERR_TIMEOUT       - uzytkownik nie przylozyl / nie zabral palca w czasie
//   ESP_ERR_INVALID_STATE - dwa przylozenia to rozne palce
//   ESP_FAIL              - zly obraz albo blad zapisu w czytniku
//   inne                  - blad komunikacji z czytnikiem
esp_err_t fp_enroll(uint16_t *out_id, fp_enroll_cb_t on_step);