#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

//from ESP32 POV
#define I2C_SDA_PIN GPIO_NUM_21
#define I2C_SCL_PIN GPIO_NUM_22


void app_main(void)
{
    printf("\n -------- Display test -------- \n");

    // HAL Initialization
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = I2C_SDA_PIN;
    u8g2_esp32_hal.bus.i2c.scl = I2C_SCL_PIN;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    //main lib element
    u8g2_t u8g2;


    //u8g2 Setup
    u8g2_Setup_sh1106_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0, //no rotation
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);

    //Screen I2C address
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
    
    printf("Screen booting...\n");

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); //turn off power-save

    u8g2_ClearBuffer(&u8g2); //RAM buffer

    u8g2_SetFont(&u8g2, u8g2_font_8x13O_tf);
    u8g2_DrawStr(&u8g2, 10, 25, "Stop Zydowskim pomowieniom!");

    u8g2_SetFont(&u8g2, u8g2_font_5x7_t_cyrillic);
    u8g2_DrawStr(&u8g2, 5, 45, "ебать ниггеров");
    
    u8g2_DrawHLine(&u8g2, 5, 52, 118);
    u8g2_DrawFrame(&u8g2, 0, 0, 128, 64);

    u8g2_SendBuffer(&u8g2);

    printf("\n ==== Display should work ==== \n");
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
