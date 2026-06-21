#pragma once
#include "u8g2.h"
#include <stdbool.h>

// Definicje pinow enkodera
#define ENC_SIA_PIN GPIO_NUM_27
#define ENC_SIB_PIN GPIO_NUM_26
#define ENC_SW_PIN  GPIO_NUM_25

void ui_init(void);
void ui_draw_menu(u8g2_t *u8g2, const char* title, const char* items[], int selected_index, int total_items);
int ui_get_encoder_direction(void);
bool ui_is_button_pressed(void);