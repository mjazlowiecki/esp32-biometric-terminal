#include "fingerprint.h"
#include "signal.h"
#include "ui.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SDA_PIN GPIO_NUM_21
#define I2C_SCL_PIN GPIO_NUM_22

typedef struct {
    uint8_t id;
    char name[20];
    bool requires_auth_to_stop;
} ActivityType;

ActivityType available_activities[] = {
    {1, "Praca", true},
    {2, "Nauka", true},
    {3, "Przerwa", false}   
};
#define NUM_ACTIVITIES 3

typedef struct {
    uint8_t user_id;
    uint8_t activity_id;
    uint32_t start_time;
} ActiveSession;

#define MAX_CONCURRENT_USERS 5
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

uint8_t next_enroll_id = 1; // Zaczynamy wpisywanie do bazy od ID 1
SystemState current_state = STATE_IDLE; 
uint8_t current_scanned_user_id = 0; 
char current_selected_activity[20] = ""; // remember selected activity


u8g2_t u8g2;

void oled_init() {
    // 1. Konfiguracja sprzętowego I2C przez HAL z biblioteki u8g2
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

void app_main(void)
{
    printf("\nStart Systemu: Terminal Biometryczny IoT\n");

    ui_init();
    sygnalizacja_init();
    oled_init(); 
    fingerprint_init();
    sdcard_init();

    while(1) {
        switch(current_state) {
           case STATE_IDLE:
                // 1. Sprawdzamy czy uzytkownik kliknal enkoder (Wyzwolenie trybu dodawania)
                if (ui_is_button_pressed()) {
                    printf("[IDLE] Wykryto klikniecie! Przechodze do trybu ENROLL...\n");
                    current_state = STATE_ENROLL;
                    break;
                }

                // 2. Normalne skanowanie w poszukiwaniu autoryzacji
                int finger_status = fingerprint_scan();
                
                if (finger_status >= 0) {
                    printf("[AUTH] Znaleziono uzytkownika! ID: %d\n", finger_status);
                    current_scanned_user_id = finger_status;
                    led_green(true); buzzer_beep_success(); led_green(false);
                    u8g2_ClearBuffer(&u8g2);
                    current_state = STATE_MENU_MAIN; 
                } else if (finger_status == -3) {
                    printf("[AUTH] Odmowa dostepu - palec nieznany!\n");
                    led_red(true); buzzer_beep_error(); led_red(false);
                }
                break;

            case STATE_ENROLL:
                // Instrukcje dla uzytkownika na ekranie
                u8g2_ClearBuffer(&u8g2);
                u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
                u8g2_DrawStr(&u8g2, 0, 15, "TRYB DODAWANIA");
                u8g2_SetFont(&u8g2, u8g2_font_helvR08_tr);
                u8g2_DrawStr(&u8g2, 0, 40, "1. Przyloz palec");
                u8g2_SendBuffer(&u8g2);

                // Funkcja blokujaca - czekamy na wykonanie operacji
                if (fingerprint_enroll(next_enroll_id)) {
                    // Sukces
                    led_green(true); buzzer_beep_success(); led_green(false);
                    next_enroll_id++; // Przygotowanie kolejnego ID
                } else {
                    // Blad procedury
                    led_red(true); buzzer_beep_error(); led_red(false);
                }
                
                // Koniec operacji, wymazanie instrukcji i powrot
                u8g2_ClearBuffer(&u8g2);
                current_state = STATE_IDLE; 
                break;
            case STATE_AUTH:
                // TODO: communication UART with R307s
                printf("[AUTH] Fingerprint verification...\n");
                // if success -> save ID to current_scanned_user_id
                // check if ID has an active session in active_sessions[]                
                // -> if NO: current_state = STATE_MENU_MAIN;
                // -> if YES: current_state = STATE_MENU_ACTIVE;
                vTaskDelay(pdMS_TO_TICKS(1000)); // delay simulation
                break;
           case STATE_MENU_MAIN: {
                static int selected_idx = 0; 
                
                // rotation
                int dir = ui_get_encoder_direction();
                if (dir == 1) {
                    selected_idx++;
                    if (selected_idx >= NUM_ACTIVITIES) selected_idx = 0;
                } else if (dir == -1) {
                    selected_idx--;
                    if (selected_idx < 0) selected_idx = NUM_ACTIVITIES - 1;
                }

                // drawing
                const char* menu_names[NUM_ACTIVITIES];
                for(int i=0; i<NUM_ACTIVITIES; i++) {
                    menu_names[i] = available_activities[i].name;
                }
                ui_draw_menu(&u8g2, "Wybierz aktywnosc:", menu_names, selected_idx, NUM_ACTIVITIES);

                // click
                if (ui_is_button_pressed()) {
                    printf("[MENU MAIN] Wybrano: %s\n", available_activities[selected_idx].name);
                    
                    // Kopiujemy nazwe wybranej aktywnosci do zmiennej globalnej, by uzyc jej w zapisie
                    strncpy(current_selected_activity, available_activities[selected_idx].name, sizeof(current_selected_activity)-1);
                    
                    led_green(true); buzzer_beep_success(); led_green(false);

                    current_state = STATE_SYNC_START;
                    u8g2_ClearBuffer(&u8g2); 
                    u8g2_SendBuffer(&u8g2); 
                }
                break;
            }

            case STATE_MENU_ACTIVE:
                printf("[MENU ACTIVE] Session in progress. Choose: Finish lub Break...\n");
                // TODO: encoder + OLED tasks
                // if finish was selected -> current_state = STATE_SYNC_STOP;
                break;

          case STATE_SYNC_START:
                printf("[SYNC] Zapisuje start aktywnosci do bufora offline...\n");
                
                // NOWE: Wywołanie zapisu na kartę MicroSD!
                sdcard_log_activity(current_scanned_user_id, current_selected_activity, "START");
                
                // Wyswietlenie powiadomienia na ekranie
                u8g2_ClearBuffer(&u8g2);
                u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
                u8g2_DrawStr(&u8g2, 0, 30, "ZAPISANO NA SD!");
                u8g2_SendBuffer(&u8g2);
                vTaskDelay(pdMS_TO_TICKS(1500)); // Pokaz komunikat przez chwile
                
                current_state = STATE_IDLE; // Powrot do ekranu glownego czuwania
                break;
            case STATE_SYNC_STOP:
                printf("[SYNC] Saving finished activity data (MQTT/SD)...\n");
                // TODO: save the buffer to SD or server via MQTT
                current_state = STATE_IDLE;
                break;
        }
                vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}