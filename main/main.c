#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


//Mock data structs
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

//System status:
typedef enum {
    STATE_IDLE,         //ekran glowny, stoper w tle, czekanie na palec
    STATE_AUTH,         //ktos przylozyl palec - odpytywanie przez R307s
    STATE_MENU_MAIN,    //nowa aktywnosc (brak aktywnej sesji)
    STATE_MENU_ACTIVE,  //podręczne menu użytkownika, (Zakończ/Przerwa/staty)
    STATE_SYNC_START,   //Wysylka MQTT / zapis na karcie SD przy starcie
    STATE_SYNC_STOP     //Wysylka MQTT / zapis na karcie SD przy zakonczeniu
} SystemState;

//global variables for state machine
SystemState current_state = STATE_IDLE;
uint8_t current_scanned_user_id = 0; //finger Id - of user which just was granted access.


//main system loop
void app_main(void)
{
    printf("\nStart Systemu: Terminal Biometryczny IoT\n");

    // modules init-s //TODO
    // i2c_master_init();
    // oled_init();
    // fingerprint_init();
    // encoder_init();

    while(1) {
        switch(current_state) {
            case STATE_IDLE:
                // TODO: refresh OLED every 1s (clock +  WiFi)
                    // TODO: if session is active - indicate
                    
                    // simulation
                    /*
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    printf("[IDLE] finger detected!\n");
                    current_state = STATE_AUTH;
                    */
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
            case STATE_MENU_MAIN:
                printf("[MENU MAIN] Pick activity! Use rotation controler m8...\n");
                // TODO: encoder + OLED tasks
                // if selected -> current_state = STATE_SYNC_START;
                break;

            case STATE_MENU_ACTIVE:
                printf("[MENU ACTIVE] Session in progress. Choose: Finish lub Break...\n");
                // TODO: encoder + OLED tasks
                // if finish was selected -> current_state = STATE_SYNC_STOP;
                break;

            case STATE_SYNC_START:
                printf("[SYNC] Saving activity data (MQTT/SD)...\n");
                // TODO: save the buffer to SD or server via MQTT
                current_state = STATE_IDLE;
                break;

            case STATE_SYNC_STOP:
                printf("[SYNC] Saving finished activity data (MQTT/SD)...\n");
                // TODO: save the buffer to SD or server via MQTT
                current_state = STATE_IDLE;
                break;
        }
    }
        vTaskDelay(pdMS_TO_TICKS(1000)); 
}