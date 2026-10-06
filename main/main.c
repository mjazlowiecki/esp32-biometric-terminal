#include "fingerprint.h"
#include "esp_timer.h"
#include "sdcard.h"
#include "signal.h" 
#include "ui.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "wifi_mqtt.h"
#include "zegar_rtc.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SDA_PIN GPIO_NUM_21
#define I2C_SCL_PIN GPIO_NUM_22

typedef struct {
    uint8_t user_id;
    char activity_name[MAX_ACTIVITY_NAME_LEN];
    uint32_t start_time_seconds; // Czas rozpoczecia sesji 
    bool is_active;
} ActiveSession;

#define MAX_CONCURRENT_USERS 10
ActiveSession active_sessions[MAX_CONCURRENT_USERS];

typedef enum {
    STATE_IDLE,         
    STATE_AUTH,
    STATE_ENROLL,         
    STATE_MENU_MAIN,    
    STATE_MENU_ACTIVE,  
    STATE_SYNC_START,   
    STATE_SYNC_STOP     
} SystemState;

uint8_t next_enroll_id = 1; 
SystemState current_state = STATE_IDLE; 
uint8_t current_scanned_user_id = 0; 
char current_selected_activity[20] = ""; 
int active_session_index = -1; // Indeks znalezionej aktywnej sesji (-1 = brak)

u8g2_t u8g2;

void init_sessions() {
    for(int i=0; i<MAX_CONCURRENT_USERS; i++) active_sessions[i].is_active = false;
}

int find_session(uint8_t user_id) {
    for(int i=0; i<MAX_CONCURRENT_USERS; i++) {
        if(active_sessions[i].is_active && active_sessions[i].user_id == user_id) return i;
    }
    return -1;
}

int get_free_session_slot() {
    for(int i=0; i<MAX_CONCURRENT_USERS; i++) {
        if(!active_sessions[i].is_active) return i;
    }
    return -1;
}

int get_active_users_count() {
    int count = 0;
    for(int i=0; i<MAX_CONCURRENT_USERS; i++) {
        if(active_sessions[i].is_active) count++;
    }
    return count;
}

//(sekundy od startu ukladu) - przydatny do obliczania czasu trwania sesji
uint32_t get_uptime_seconds() {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}


void oled_init() {
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = I2C_SDA_PIN;
    u8g2_esp32_hal.bus.i2c.scl = I2C_SCL_PIN;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_Setup_sh1106_i2c_128x64_noname_f(
        &u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
}

void draw_centered_str(u8g2_t *u8g2, int y, const char *str) {
    int w = u8g2_GetStrWidth(u8g2, str);
    int x = (128 - w) / 2;
    u8g2_DrawStr(u8g2, x, y, str);
}

void app_main(void)
{
    printf("\nStart Systemu: Terminal Biometryczny IoT\n");

    ui_init();
    sygnalizacja_init();
    oled_init(); 
    fingerprint_init();
    init_sessions();
    sdcard_init(); 
    
    wifi_mqtt_init();
    zegar_rtc_init(); // Zegar WiFi (SNTP)

    uint32_t last_idle_refresh = 0;

    while(1) {
        if (new_cloud_cmd) {
            new_cloud_cmd = false; 
            led_yellow(true); buzzer_beep_error(); buzzer_beep_error(); led_yellow(false);
            u8g2_ClearBuffer(&u8g2);
            u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
            draw_centered_str(&u8g2, 20, "KOMUNIKAT:");
            u8g2_SetFont(&u8g2, u8g2_font_helvR08_tr);
            draw_centered_str(&u8g2, 45, cloud_cmd_buf); 
            u8g2_SendBuffer(&u8g2);
            vTaskDelay(pdMS_TO_TICKS(3000));
            current_state = STATE_IDLE; 
        }
        
        switch(current_state) {
           case STATE_IDLE: {
                // Odświeżaj ekran co 1 sekundę
                uint32_t now = get_uptime_seconds();
                if (now - last_idle_refresh >= 1) {
                    last_idle_refresh = now;
                    
                    char time_buf[32];
                    rtc_get_timestamp(time_buf, sizeof(time_buf)); // Pobranie czasu z SNTP
                    
                    // Skracamy czas np. "2024-06-25 10:30:15" do "10:30"
                    char short_time[10] = "--:--";
                    if (strlen(time_buf) > 15) {
                        strncpy(short_time, time_buf + 11, 5);
                        short_time[5] = '\0';
                    }

                    u8g2_ClearBuffer(&u8g2);
                    // Pasek gorny
                    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
                    u8g2_DrawStr(&u8g2, 0, 10, "Terminal IoT");
                    u8g2_DrawStr(&u8g2, 95, 10, short_time);
                    u8g2_DrawHLine(&u8g2, 0, 12, 128);

                    // Glowny komunikat
                    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
                    draw_centered_str(&u8g2, 35, "GOTOWY");
                    
                    // Informacja na dole
                    char bottom_info[32];
                    snprintf(bottom_info, sizeof(bottom_info), "Aktywni: %d", get_active_users_count());
                    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
                    draw_centered_str(&u8g2, 55, bottom_info);
                    draw_centered_str(&u8g2, 64, "(Wcisnij by dodac palec)");
                    u8g2_SendBuffer(&u8g2);
                }

                if (ui_is_button_pressed()) {
                    current_state = STATE_ENROLL;
                    break;
                }

                int finger_status = fingerprint_scan();
                
                if (finger_status >= 0) {
                    current_scanned_user_id = finger_status;
                    led_green(true); buzzer_beep_success(); led_green(false);
                    
                    active_session_index = find_session(current_scanned_user_id);
                    
                    u8g2_ClearBuffer(&u8g2);
                    if (active_session_index != -1) {
                        current_state = STATE_MENU_ACTIVE; // JUZ PRACUJE -> Ekran zamykania sesji
                    } else {
                        current_state = STATE_MENU_MAIN;   // NOWY -> Ekran wyboru
                    }
                } else if (finger_status == -3) {
                    u8g2_ClearBuffer(&u8g2);
                    u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
                    draw_centered_str(&u8g2, 35, "ODMAWA DOSTEPU");
                    u8g2_SendBuffer(&u8g2);
                    led_red(true); buzzer_beep_error(); led_red(false);
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    last_idle_refresh = 0; // Wymus odswiezenie
                }
                break;
            }

            case STATE_ENROLL:
                u8g2_ClearBuffer(&u8g2);
                u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
                draw_centered_str(&u8g2, 20, "DODAJ UZYTKOWNIKA");
                u8g2_SetFont(&u8g2, u8g2_font_helvR08_tr);
                char enroll_msg[32];
                snprintf(enroll_msg, sizeof(enroll_msg), "Nowe ID: %d", next_enroll_id);
                draw_centered_str(&u8g2, 40, enroll_msg);
                draw_centered_str(&u8g2, 55, "Przyloz palec...");
                u8g2_SendBuffer(&u8g2);

                if (fingerprint_enroll(next_enroll_id)) {
                    u8g2_ClearBuffer(&u8g2);
                    u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
                    draw_centered_str(&u8g2, 35, "ZAPISANO!");
                    u8g2_SendBuffer(&u8g2);
                    led_green(true); buzzer_beep_success(); led_green(false);
                    next_enroll_id++; 
                } else {
                    u8g2_ClearBuffer(&u8g2);
                    u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
                    draw_centered_str(&u8g2, 35, "BLAD ZAPISU");
                    u8g2_SendBuffer(&u8g2);
                    led_red(true); buzzer_beep_error(); led_red(false);
                }
                vTaskDelay(pdMS_TO_TICKS(1500));
                last_idle_refresh = 0;
                current_state = STATE_IDLE; 
                break;
                
            case STATE_AUTH:
                current_state = STATE_IDLE;
                break;
                
           case STATE_MENU_MAIN: {
                static int selected_idx = 0; 
                
                int dir = ui_get_encoder_direction();
                if (dir == 1) {
                    selected_idx++;
                    if (selected_idx >= dynamic_activities_count) selected_idx = 0;
                } else if (dir == -1) {
                    selected_idx--;
                    if (selected_idx < 0) selected_idx = dynamic_activities_count - 1;
                }

                char header_msg[32];
                snprintf(header_msg, sizeof(header_msg), "Witaj ID: %d", current_scanned_user_id);
                
                const char* menu_names[MAX_ACTIVITIES]; 
                for(int i = 0; i < dynamic_activities_count; i++) {
                    menu_names[i] = dynamic_activities[i];
                }
                ui_draw_menu(&u8g2, header_msg, menu_names, selected_idx, dynamic_activities_count);

                if (ui_is_button_pressed()) {
                    strncpy(current_selected_activity, dynamic_activities[selected_idx], sizeof(current_selected_activity)-1);
                    led_green(true); buzzer_beep_success(); led_green(false);
                    current_state = STATE_SYNC_START;
                }
                break;
            }

            case STATE_MENU_ACTIVE: {
                // UZYTKOWNIK MA AKTYWNA SESJE
                static int action_idx = 0; // 0 = Zakoncz, 1 = Anuluj
                int dir = ui_get_encoder_direction();
                if (dir != 0) {
                    action_idx = !action_idx; // Przelacz miedzy 0 a 1
                }
                
                ActiveSession *sess = &active_sessions[active_session_index];
                uint32_t duration_sec = get_uptime_seconds() - sess->start_time_seconds;
                int dur_min = duration_sec / 60;
                int dur_sec = duration_sec % 60;

                u8g2_ClearBuffer(&u8g2);
                u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
                char head[32]; snprintf(head, sizeof(head), "ID: %d w trakcie", current_scanned_user_id);
                u8g2_DrawStr(&u8g2, 0, 10, head);
                u8g2_DrawHLine(&u8g2, 0, 12, 128);
                
                char act_info[32]; snprintf(act_info, sizeof(act_info), "[%s]", sess->activity_name);
                draw_centered_str(&u8g2, 25, act_info);
                
                char time_info[32]; snprintf(time_info, sizeof(time_info), "Trwa: %02dm %02ds", dur_min, dur_sec);
                draw_centered_str(&u8g2, 38, time_info);

                // Opcje Menu:
                u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
                u8g2_DrawStr(&u8g2, 10, 58, action_idx == 0 ? "> Zakoncz <" : "  Zakoncz  ");
                u8g2_DrawStr(&u8g2, 85, 58, action_idx == 1 ? "> Anuluj <" : "  Anuluj  ");
                u8g2_SendBuffer(&u8g2);

                if (ui_is_button_pressed()) {
                    if (action_idx == 0) {
                        // Zakoncz
                        strncpy(current_selected_activity, sess->activity_name, sizeof(current_selected_activity)-1);
                        sess->is_active = false; // ZWALNIAMY SESJE
                        led_green(true); buzzer_beep_success(); led_green(false);
                        current_state = STATE_SYNC_STOP;
                    } else {
                        // Anuluj powrot
                        last_idle_refresh = 0;
                        current_state = STATE_IDLE;
                    }
                    action_idx = 0; // Reset na przyszlosc
                }
                break;
            }

          case STATE_SYNC_START: {
                int slot = get_free_session_slot();
                if (slot != -1) {
                    active_sessions[slot].is_active = true;
                    active_sessions[slot].user_id = current_scanned_user_id;
                    strncpy(active_sessions[slot].activity_name, current_selected_activity, MAX_ACTIVITY_NAME_LEN-1);
                    active_sessions[slot].start_time_seconds = get_uptime_seconds();
                }

                sdcard_log_activity(current_scanned_user_id, current_selected_activity, "START");
                mqtt_send_log(current_scanned_user_id, current_selected_activity, "START");
                
                u8g2_ClearBuffer(&u8g2);
                u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
                draw_centered_str(&u8g2, 25, "ROZPOCZETO");
                u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
                draw_centered_str(&u8g2, 45, current_selected_activity);
                draw_centered_str(&u8g2, 60, "(Zapisano na SD/Chmura)");
                u8g2_SendBuffer(&u8g2);
                
                vTaskDelay(pdMS_TO_TICKS(2000)); 
                last_idle_refresh = 0;
                current_state = STATE_IDLE; 
                break;
            }
                
            case STATE_SYNC_STOP:
                sdcard_log_activity(current_scanned_user_id, current_selected_activity, "STOP");
                mqtt_send_log(current_scanned_user_id, current_selected_activity, "STOP");
                
                u8g2_ClearBuffer(&u8g2);
                u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
                draw_centered_str(&u8g2, 35, "ZAKONCZONO");
                u8g2_SendBuffer(&u8g2);
                
                vTaskDelay(pdMS_TO_TICKS(1500));
                last_idle_refresh = 0;
                current_state = STATE_IDLE;
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}