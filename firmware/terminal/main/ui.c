#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "activities.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_store.h"
#include "fingerprint.h"
#include "indicators.h"
#include "net.h"
#include "timesync.h"

static const char *TAG = "ui";

#define UI_MESSAGE_MS        2000  // jak dlugo widac komunikat
#define UI_CHOOSE_TIMEOUT_MS 15000 // powrot z listy po 15 s bez ruchu
#define UI_LIST_ROWS         4     // ile pozycji listy miesci sie na ekranie
#define UI_LINE_LEN          22    // 128 px / 6 px na znak = 21 znakow + '\0'

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

static bool s_finger_armed; // czujnik byl pusty od wejscia do UI_MAIN
static bool s_fp_failing;   // czytnik zglasza bledy (do jednorazowego logu)
static uint16_t s_user_id;  // kto jest obslugiwany (po rozpoznaniu)
static size_t s_cursor;     // zaznaczona pozycja listy
static char s_msg1[UI_LINE_LEN];
static char s_msg2[UI_LINE_LEN];

static void ui_enter(ui_state_t state)
{
    ESP_LOGI(TAG, "stan %d -> %d", s_state, state);
    s_state      = state;
    s_entered_us = esp_timer_get_time();
    s_dirty      = true;

    if (state == UI_MAIN)
    {
        s_finger_armed = false; // najpierw musi zobaczyc pusty czujnik
    }
}

static uint32_t ui_ms_in_state(void)
{
    return (uint32_t)((esp_timer_get_time() - s_entered_us) / 1000);
}

// uzytkownik cos zrobil: licz timeout stanu od nowa i przerysuj
static void ui_touch(void)
{
    s_entered_us = esp_timer_get_time();
    s_dirty      = true;
}

static void ui_message(const char *line1, const char *line2)
{
    snprintf(s_msg1, sizeof(s_msg1), "%s", line1);
    snprintf(s_msg2, sizeof(s_msg2), "%s", line2);
    ui_enter(UI_MESSAGE);
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

static void draw_choose(void)
{
    int w        = u8g2_GetDisplayWidth(s_d);
    size_t n     = activities_count();
    size_t first = (s_cursor >= UI_LIST_ROWS) ? s_cursor - (UI_LIST_ROWS - 1) : 0;

    u8g2_ClearBuffer(s_d);
    u8g2_SetFont(s_d, u8g2_font_6x10_tr);

    char header[UI_LINE_LEN];
    snprintf(header, sizeof(header), "ID %u: wybierz", s_user_id);
    u8g2_DrawStr(s_d, 0, 8, header);
    u8g2_DrawHLine(s_d, 0, 10, w);

    for (size_t row = 0; row < UI_LIST_ROWS && first + row < n; row++)
    {
        const activity_t *a = activities_get(first + row);
        int y               = 21 + row * 11;

        char limit[12] = "";
        if (a->max_min != ACTIVITY_NO_LIMIT)
        {
            snprintf(limit, sizeof(limit), "max %um", a->max_min);
        }
        else if (a->min_min != ACTIVITY_NO_LIMIT)
        {
            snprintf(limit, sizeof(limit), "min %um", a->min_min);
        }

        bool selected = (first + row == s_cursor);
        if (selected)
        {
            u8g2_DrawBox(s_d, 0, y - 9, w, 11); // jasny pasek pod zaznaczeniem
            u8g2_SetDrawColor(s_d, 0);          // tekst "ciemny na jasnym"
        }
        u8g2_DrawStr(s_d, 2, y, a->name);
        u8g2_DrawStr(s_d, w - u8g2_GetStrWidth(s_d, limit) - 2, y, limit);
        u8g2_SetDrawColor(s_d, 1);
    }

    u8g2_SendBuffer(s_d);
}

static void draw_message(void)
{
    int w = u8g2_GetDisplayWidth(s_d);
    u8g2_ClearBuffer(s_d);
    u8g2_SetFont(s_d, u8g2_font_6x10_tr);
    u8g2_DrawFrame(s_d, 0, 0, w, u8g2_GetDisplayHeight(s_d));
    u8g2_DrawStr(s_d, (w - u8g2_GetStrWidth(s_d, s_msg1)) / 2, 28, s_msg1);
    u8g2_DrawStr(s_d, (w - u8g2_GetStrWidth(s_d, s_msg2)) / 2, 44, s_msg2);
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
        case UI_CHOOSE:
            draw_choose();
            break;
        case UI_MESSAGE:
            draw_message();
            break;
    }
}

static void main_check_finger(void)
{
    if (!s_finger_armed)
    {
        // Czekamy na pusty czujnik - wystarczy samo zdjecie, bez szukania w bazie
        if (fp_capture() == ESP_ERR_NOT_FOUND)
        {
            s_finger_armed = true;
        }
        return;
    }

    fp_id_result_t res;
    uint16_t id, score;
    esp_err_t err = fp_identify(&res, &id, &score);

    if (err != ESP_OK)
    {
        if (!s_fp_failing)
        {
            ESP_LOGW(TAG, "Czytnik nie odpowiada: %s", esp_err_to_name(err));
            s_fp_failing = true;
        }
        return;
    }
    if (s_fp_failing)
    {
        ESP_LOGI(TAG, "Czytnik znowu odpowiada");
        s_fp_failing = false;
    }

    switch (res)
    {
        case FP_ID_MATCH:
            ESP_LOGI(TAG, "Rozpoznano ID %u (wynik %u)", id, score);
            s_user_id = id;
            s_cursor  = 0;
            indicate_success();
            ui_enter(UI_CHOOSE);
            break;
        case FP_ID_NO_MATCH:
            indicate_error();
            ui_message("Nie rozpoznano", "Sprobuj ponownie");
            break;
        case FP_ID_NO_FINGER:
            break;
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
            main_check_finger();
            if (now != s_last_sec)
            {
                s_last_sec = now;
                s_dirty    = true; // nowa sekunda zegara
            }
            break;
        }
        case UI_CHOOSE:
            if (ui_ms_in_state() > UI_CHOOSE_TIMEOUT_MS)
            {
                ui_enter(UI_MAIN);
            }
            break;
        case UI_MESSAGE:
            if (ui_ms_in_state() > UI_MESSAGE_MS)
            {
                ui_enter(UI_MAIN);
            }
            break;
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
    switch (s_state)
    {
        case UI_MAIN:
            break; // dlugie wcisniecie = rejestracja (krok 65)

        case UI_CHOOSE:
            if (ev == ENC_EVENT_RIGHT && s_cursor + 1 < activities_count())
            {
                s_cursor++;
                ui_touch();
            }
            else if (ev == ENC_EVENT_LEFT && s_cursor > 0)
            {
                s_cursor--;
                ui_touch();
            }
            else if (ev == ENC_EVENT_PRESS)
            {
                const activity_t *a = activities_get(s_cursor);
                uint32_t seq;
                if (event_store_append(s_user_id, a->id, EVENT_ACTION_START, &seq) == ESP_OK)
                {
                    ESP_LOGI(TAG, "START #%lu: ID %u, %s", (unsigned long)seq, s_user_id, a->name);
                    indicate_success();
                    ui_message("Rozpoczeto:", a->name);
                }
                else
                {
                    indicate_error();
                    ui_message("Blad zapisu!", "Zdarzenie NIE zapisane");
                }
            }
            else if (ev == ENC_EVENT_LONG_PRESS)
            {
                ui_enter(UI_MAIN); // wstecz
            }
            break;

        case UI_MESSAGE:
            if (ev == ENC_EVENT_PRESS)
            {
                ui_enter(UI_MAIN); // klik skraca komunikat
            }
            break;
    }
}