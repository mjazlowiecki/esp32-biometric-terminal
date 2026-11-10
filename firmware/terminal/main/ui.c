#include "ui.h"

#include <stdbool.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "net.h"
#include "timesync.h"

static const char *TAG = "ui";

typedef enum
{
    UI_MAIN,    // ekran glowny: zegar, status, "Przyloz palec"
    UI_CHOOSE,  // wybor aktywnosci (63b)
    UI_MESSAGE, // komunikat na chwile, potem powrot (63b)
} ui_state_t;

static u8g2_t *s_d;
static ui_state_t s_state;
static int64_t s_entered_us; // kiedy weszlismy do biezacego stanu
static bool s_dirty;         // ekran do przerysowania

static void ui_enter(ui_state_t state)
{
    ESP_LOGI(TAG, "stan %d -> %d", s_state, state);
    s_state      = state;
    s_entered_us = esp_timer_get_time();
    s_dirty      = true;
}

static uint32_t ui_ms_in_state(void)
{
    return (uint32_t)((esp_timer_get_time() - s_entered_us) / 1000);
}

static void draw_main(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    char time_str[9];  // "HH:MM:SS" + '\0'
    char date_str[11]; // "DD.MM.YYYY" + '\0'
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &t);
    strftime(date_str, sizeof(date_str), "%d.%m.%Y", &t);

    int w = u8g2_GetDisplayWidth(s_d);
    u8g2_ClearBuffer(s_d);

    u8g2_SetFont(s_d, u8g2_font_6x10_tr);
    u8g2_DrawStr(s_d, 0, 8, net_is_connected() ? "WiFi" : "offline");
    if (!timesync_is_valid())
    {
        u8g2_DrawStr(s_d, w - u8g2_GetStrWidth(s_d, "czas?"), 8, "czas?");
    }

    u8g2_SetFont(s_d, u8g2_font_logisoso24_tn);
    u8g2_DrawStr(s_d, (w - u8g2_GetStrWidth(s_d, time_str)) / 2, 36, time_str);

    u8g2_SetFont(s_d, u8g2_font_6x10_tr);
    u8g2_DrawStr(s_d, (w - u8g2_GetStrWidth(s_d, date_str)) / 2, 48, date_str);
    u8g2_DrawStr(s_d, (w - u8g2_GetStrWidth(s_d, "Przyloz palec")) / 2, 62, "Przyloz palec");

    u8g2_SendBuffer(s_d);
}

static void ui_draw(void)
{
    switch (s_state)
    {
        case UI_MAIN:
            draw_main();
            break;
        default:
            break; // pozostale stany: 63b
    }
}

void ui_tick(void)
{
    switch (s_state)
    {
        case UI_MAIN:
        {
            static time_t s_last_sec;
            time_t now = time(NULL);
            if (now != s_last_sec)
            {
                s_last_sec = now;
                s_dirty    = true; // nowa sekunda zegara
            }
            break;
        }
        default:
            break;
    }

    if (s_dirty)
    {
        ui_draw();
        s_dirty = false;
    }
}

void ui_init(u8g2_t *display)
{
    s_d = display;
    ui_enter(UI_MAIN);
    ui_tick(); // od razu narysuj ekran
}

void ui_on_encoder(encoder_event_t ev)
{
    ESP_LOGI(TAG, "enkoder: zdarzenie %d w stanie %d", ev, s_state);
    // przejscia: 63b
}