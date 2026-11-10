#pragma once

#include "encoder.h"
#include "u8g2.h"

#define UI_TICK_MS 250 // jak czesto wolac ui_tick()

void ui_init(u8g2_t *display);
void ui_on_encoder(encoder_event_t ev);
void ui_tick(void);