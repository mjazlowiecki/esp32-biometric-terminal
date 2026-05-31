#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define ENC_SIA GPIO_NUM_27
#define ENC_SIB GPIO_NUM_26
#define ENC_SW GPIO_NUM_25

void app_main(void)
{
    //configuration struct
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, // disable interrupts as we probre in loop
        .mode = GPIO_MODE_INPUT,        // PINs as Input

        //Pion configuration bitmask
        .pin_bit_mask = (1ULL << ENC_SIA ) | (1ULL << ENC_SIB) | (1ULL << ENC_SW),
        .pull_down_en = 0,      //disable pulldown
        .pull_up_en = 1         //enable pullup to 3.3V
    };

    gpio_config(&io_conf);
    
    //variables for the encoder

    int last_sia = gpio_get_level(ENC_SIA);   //store the SIA initial state
    int counter = 0;                          //rotation counter

    //program loop
    while(1) {

        int current_sia = gpio_get_level(ENC_SIA);
        int current_sib = gpio_get_level(ENC_SIB);
        int current_sw = gpio_get_level(ENC_SW);

        //rotation logic
        //we look for the moment where sia switches from '1' to '0' (falling edge)
        
        if(last_sia == 1 && current_sia == 0)
        {
            //if SIB = '0' we are turniing to the right
            if(current_sib == 0)
            {
                counter++;
                printf("Right (CW) | Counter %d\n", counter);
            } else // if SIB is '1' that means we are turning left
            {
                counter--;
                printf("Left (CW) | Counter %d\n", counter);
            }
        }
            //remember the sia state to use it in next loop iteration
            last_sia = current_sia;

            if(current_sw == 0)
            {
                printf("Button pressed! - Resetting counter...");
                counter = 0;                          //Reset Counter
                vTaskDelay(200 / portTICK_PERIOD_MS); //Simple debouncing   
            }
            //FreeRTOS delay 
            vTaskDelay(pdMS_TO_TICKS(10));
        }
}
