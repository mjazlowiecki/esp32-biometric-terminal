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
#include "sessions.h"
#include "timesync.h"

static const char *TAG = "ui";

#define UI_MESSAGE_MS        2000  // jak dlugo widac komunikat
#define UI_CHOOSE_TIMEOUT_MS 15000 // powrot z listy po 15 s bez ruchu
#define UI_LIST_ROWS         4     // ile pozycji listy miesci sie na ekranie
#define UI_LINE_LEN          22    // 128 px / 6 px na znak = 21 znakow + '\0'
#define UI_ACTIVE_ROWS       2     // pozycje menu widoczne na ekranie aktywnosci

typedef enum
{
    UI_MAIN,    // ekran glowny: zegar, status, "Przyloz palec"
    UI_CHOOSE,  // wybor aktywnosci
    UI_MESSAGE, // komunikat na chwile, potem powrot
    UI_ACTIVE,  // trwajaca aktywnosc uzytkownika
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

static size_t list_first(size_t cursor, size_t rows)
{
    return (cursor >= rows) ? cursor - (rows - 1) : 0;
}

static void draw_row(int y, const char *left, const char *right, bool selected)
{
    int w = u8g2_GetDisplayWidth(s_d);
    if (selected)
    {
        u8g2_DrawBox(s_d, 0, y - 9, w, 11);
        u8g2_SetDrawColor(s_d, 0);
    }
    u8g2_DrawStr(s_d, 2, y, left);
    u8g2_DrawStr(s_d, w - u8g2_GetStrWidth(s_d, right) - 2, y, right);
    u8g2_SetDrawColor(s_d, 1); // ZAWSZE przywroc - kolor to stan globalny biblioteki
}

static void ui_message(const char *line1, const char *line2)
{
    snprintf(s_msg1, sizeof(s_msg1), "%s", line1);
    snprintf(s_msg2, sizeof(s_msg2), "%s", line2);
    ui_enter(UI_MESSAGE);
}

static void fmt_hms(char *buf, size_t len, int64_t sec)
{
    const int64_t max = 9999LL * 3600 + 59 * 60 + 59; // 9999:59:59 = 10 znakow
    if (sec > max)
    {
        sec = max; // sesja trwajaca ponad 416 dni i tak jest zapomniana
    }
    snprintf(buf, len, "%ld:%02d:%02d", (long)(sec / 3600), (int)(sec / 60 % 60), (int)(sec % 60));
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

    const char *hint = event_store_is_ready() ? "Przyloz palec" : "Brak karty SD!";
    u8g2_DrawStr(s_d, (w - u8g2_GetStrWidth(s_d, hint)) / 2, 62, hint);

    u8g2_SendBuffer(s_d);
}

static void draw_choose(void)
{
    int w        = u8g2_GetDisplayWidth(s_d);
    size_t n     = activities_count();
    size_t first = list_first(s_cursor, UI_LIST_ROWS);

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

        draw_row(y, a->name, limit, first + row == s_cursor);
    }

    u8g2_SendBuffer(s_d);
}

//"Zakoncz" + wszystkie aktywnosci poza trwajaca
static size_t active_menu_len(uint16_t current)
{
    return 1 + activities_count() - (activities_find(current) != NULL ? 1 : 0);
}

// poz. menu k >= 1 -> k-ta aktywnosc z pominieciem trwajacej
static const activity_t *active_menu_activity(size_t k, uint16_t current)
{
    size_t seen = 0;
    for (size_t i = 0; i < activities_count(); i++)
    {
        const activity_t *a = activities_get(i);
        if (a->id == current)
        {
            continue;
        }
        if (++seen == k)
        {
            return a;
        }
    }
    return NULL;
}

static void draw_active(void)
{
    const session_t *s  = sessions_get(s_user_id);
    const activity_t *a = (s != NULL) ? activities_find(s->activity_id) : NULL;
    int w               = u8g2_GetDisplayWidth(s_d);
    char line[UI_LINE_LEN];

    u8g2_ClearBuffer(s_d);
    u8g2_SetFont(s_d, u8g2_font_6x10_tr);

    snprintf(line, sizeof(line), "ID %u: %s", s_user_id, a ? a->name : "?");
    u8g2_DrawStr(s_d, 0, 8, line);
    u8g2_DrawHLine(s_d, 0, 10, w);

    time_t now = time(NULL);
    bool known = s != NULL && (s->flags & EVENT_FLAG_TIME_SYNCED) && timesync_is_valid() &&
                 (int64_t)now >= (int64_t)s->start;
    int64_t elapsed = known ? (int64_t)now - (int64_t)s->start : 0;

    char hms[11];
    if (known)
    {
        fmt_hms(hms, sizeof(hms), elapsed);
        snprintf(line, sizeof(line), "Trwa:      %s", hms);
    }
    else
    {
        snprintf(line, sizeof(line), "Trwa:      ?");
    }

    u8g2_DrawStr(s_d, 0, 21, line);
    if (known && a != NULL)
    {
        int64_t min_s = (int64_t)a->min_min * 60;
        int64_t max_s = (int64_t)a->max_min * 60;

        if (a->min_min != ACTIVITY_NO_LIMIT && elapsed < min_s)
        {
            fmt_hms(hms, sizeof(hms), min_s - elapsed);
            snprintf(line, sizeof(line), "Min. za:   %s", hms);
        }
        else if (a->max_min != ACTIVITY_NO_LIMIT && elapsed <= max_s)
        {
            fmt_hms(hms, sizeof(hms), max_s - elapsed);
            snprintf(line, sizeof(line), "Do limitu: %s", hms);
        }
        else if (a->max_min != ACTIVITY_NO_LIMIT)
        {
            fmt_hms(hms, sizeof(hms), elapsed - max_s);
            snprintf(line, sizeof(line), "Przekr.:   %s", hms);
        }
        else if (a->min_min != ACTIVITY_NO_LIMIT)
        {
            snprintf(line, sizeof(line), "Minimum osiagniete");
        }
        else
        {
            snprintf(line, sizeof(line), "Bez limitu");
        }
        u8g2_DrawStr(s_d, 0, 32, line);
    }
    uint16_t current = (s != NULL) ? s->activity_id : 0;
    size_t n         = active_menu_len(current);
    size_t first     = list_first(s_cursor, UI_ACTIVE_ROWS);

    for (size_t row = 0; row < UI_ACTIVE_ROWS && first + row < n; row++)
    {
        size_t k = first + row;
        if (k == 0)
        {
            snprintf(line, sizeof(line), "Zakoncz");
        }
        else
        {
            const activity_t *m = active_menu_activity(k, current);
            snprintf(line, sizeof(line), "Zmien: %s", m ? m->name : "?");
        }
        draw_row(43 + row * 11, line, "", k == s_cursor);
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

// true raz na kazda nowa sekunde zegara - wykrywanie zbocza
static bool ui_new_second(void)
{
    static time_t s_last_sec;
    time_t now = time(NULL);
    if (now != s_last_sec)
    {
        s_last_sec = now;
        return true;
    }
    return false;
}

static void ui_draw(void)
{
    switch (s_state)
    {
        case UI_MAIN:
            draw_main();
            break;
        case UI_CHOOSE:
            draw_choose();
            break;
        case UI_MESSAGE:
            draw_message();
            break;
        case UI_ACTIVE:
            draw_active();
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
            ui_enter(sessions_get(id) != NULL ? UI_ACTIVE : UI_CHOOSE);
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
            main_check_finger();
            if (ui_new_second())
            {
                s_dirty = true; // nowa sekunda zegara
            }
            break;
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
        case UI_ACTIVE:
            if (ui_new_second())
            {
                s_dirty = true; // licznik trwa, tyka
            }
            if (ui_ms_in_state() > UI_CHOOSE_TIMEOUT_MS)
            {
                ui_enter(UI_MAIN);
            }
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

static void active_press(uint16_t current)
{
    const activity_t *cur = activities_find(current);
    const char *cur_name  = cur ? cur->name : "?"; // nazwa przed stopem

    if (s_cursor == 0)
    {
        if (sessions_stop(s_user_id) == ESP_OK)
        {
            indicate_success();
            ui_message("Zakonczono:", cur_name);
        }
        else
        {
            indicate_error();
            ui_message("Blad zapisu!", "Sprawdz karte SD");
        }
        return;
    }

    const activity_t *next = active_menu_activity(s_cursor, current);
    if (next == NULL)
    {
        return; // kursor poza menu
    }

    if (sessions_start(s_user_id, next->id) == ESP_OK)
    {
        indicate_success();
        ui_message("Zmieniono na:", next->name);
    }
    else if (sessions_get(s_user_id) == NULL)
    {
        indicate_error();
        ui_message("Zakonczono, ale", "START nie zapisany!"); // STOP przeszedl, START nie
    }
    else
    {
        indicate_error();
        ui_message("Blad zapisu!", "Sprawdz karte SD");
    }
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
                if (sessions_start(s_user_id, a->id) == ESP_OK)
                {
                    indicate_success();
                    ui_message("Rozpoczeto:", a->name);
                }
                else
                {
                    indicate_error();
                    ui_message("Blad zapisu!", "Sprawdz karte SD");
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
        case UI_ACTIVE:
        {
            const session_t *s = sessions_get(s_user_id);
            uint16_t current   = (s != NULL) ? s->activity_id : 0;

            if (ev == ENC_EVENT_RIGHT && s_cursor + 1 < active_menu_len(current))
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
                active_press(current);
            }
            else if (ev == ENC_EVENT_LONG_PRESS)
            {
                ui_enter(UI_MAIN); // wstecz
            }
            break;
        }
    }
}