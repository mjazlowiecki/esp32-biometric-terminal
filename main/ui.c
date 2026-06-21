#include "ui.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//
// isr variable
volatile int encoder_counter = 0;
int last_encoder_value = 0;

//isr when movement
static void IRAM_ATTR encoder_isr_handler(void* arg) {

    //if was caused by isr, check sia pin state
    if (gpio_get_level(ENC_SIB_PIN) == 0) {
        encoder_counter++; //rotate right
    } else {
        encoder_counter--; //rotate left
    }
}

void ui_init(void) {
    gpio_set_direction(ENC_SW_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ENC_SW_PIN, GPIO_PULLUP_ONLY);

 
    gpio_set_direction(ENC_SIA_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(ENC_SIB_PIN, GPIO_MODE_INPUT);

    //configure interrupts
    gpio_install_isr_service(0);

    //falling edge interrupt from 1 to 0
    gpio_set_intr_type(ENC_SIA_PIN, GPIO_INTR_NEGEDGE);

    //func interrupt to SIA pin asign
    gpio_isr_handler_add(ENC_SIA_PIN, encoder_isr_handler, NULL);
}

// ask in loop in main
int ui_get_encoder_direction(void) {
    int dir = 0;
    int current_val = encoder_counter; // atomic read
    
    // if counter changed setup direction
    if (current_val > last_encoder_value) {
        dir = 1;
    } else if (current_val < last_encoder_value) {
        dir = -1;
    }
    
    last_encoder_value = current_val; // save state
    return dir;
}
// return true if button was pressed
bool ui_is_button_pressed(void) {
    if (gpio_get_level(ENC_SW_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50)); //sofware debouncing 50ms
        if (gpio_get_level(ENC_SW_PIN) == 0) {
            while(gpio_get_level(ENC_SW_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10)); // wait for the button to be relesased
            }
            return true;
        }
    }
    return false;
}

//draw OLED menu
void ui_draw_menu(u8g2_t *u8g2, const char* title, const char* items[], int selected_index, int total_items) {
    u8g2_ClearBuffer(u8g2);
    
    // header
    u8g2_SetFont(u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(u8g2, 0, 10, title);
    u8g2_DrawHLine(u8g2, 0, 13, 128);

    // draw, max 3 positions on the screen
    u8g2_SetFont(u8g2, u8g2_font_helvR08_tr);
    int start_y = 28;
    int line_height = 15;

    for (int i = 0; i < total_items; i++) {
        //calculate position for the text
        int y_pos = start_y + (i * line_height);
        
        // draw text
        u8g2_DrawStr(u8g2, 10, y_pos, items[i]);

        // if position was marked - draw in reverse color (backlight)
        if (i == selected_index) {
            u8g2_SetDrawColor(u8g2, 2); //XOR
            u8g2_DrawBox(u8g2, 0, y_pos - 10, 128, line_height);
            u8g2_SetDrawColor(u8g2, 1); //back to normal drawing (state Machine)
        }
    }
    u8g2_SendBuffer(u8g2);
}