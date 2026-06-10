#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
//from ESP32 POV
#define FP_TX_PIN GPIO_NUM_16 
#define FP_RX_PIN GPIO_NUM_17

//Use Hardware ESP32 UART prot
#define FP_UART_NUM UART_NUM_1
#define BUF_SIZE (1024)

void app_main(void)
{
   //configure R307s
   uart_config_t uart_config = {
    .baud_rate = 57600,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
   };

   //Initialize UART driver 
   uart_driver_install(FP_UART_NUM, BUF_SIZE *2, 0, 0, NULL, 0);
   //Load configuration
   uart_param_config(FP_UART_NUM, &uart_config);
   //Write pins to UART Interface
   uart_set_pin(FP_UART_NUM, FP_TX_PIN, FP_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

   //Handshake command definition:
   uint8_t handshake_cmd[] = {
    0xEF, 0x01,             //Header
    0xFF, 0xFF, 0xFF, 0xFF, //Default Module addr
    0x01,                   //pckg ID 0x01 = command
    0x00, 0x03,             //pckg length = 3 bytes
    0x35,                   //Instruction code 0x35 = Handshake
    0x00, 0x39              //checksum = 0x01 + 0x00 + 0x03 + 0x35 = 0x39
   };

    printf("\n--- R307S Comm test---\n");

    while(1)
    {
        printf("\nSent HandShake (0x35)...\n");
        uart_write_bytes(FP_UART_NUM, (const char *)handshake_cmd, sizeof(handshake_cmd));

        vTaskDelay(pdMS_TO_TICKS(500)); //0.5s to process

        uint8_t data[128]; // Response buffer
        int length = 0;

        uart_get_buffered_data_len(FP_UART_NUM, (size_t*)&length); //check response buffer
    if (length > 0) {
            // read array
            length = uart_read_bytes(FP_UART_NUM, data, length, pdMS_TO_TICKS(100));
            
            printf("Received %d bytes: ", length);
            for (int i = 0; i < length; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");

            // validate data
            // Acknowledge package identifier is 0x07 (6th index of array)
            if (length >= 12 && data[6] == 0x07) {
                // Confirmation code is on 9th
                uint8_t confirmation_code = data[9];
                
                if (confirmation_code == 0x00) {
                    printf("SUCCESS (0x00)!\n");
                } else {
                    printf("Error, code: 0x%02X\n", confirmation_code);
                }
            } else {
                printf("Received garbage. Check pins!\n");
            }
        } else {
            printf("No answer! Check input 5V and cables TX/RX.\n");
        }

        // wait 3 sec to send another ping
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
}
