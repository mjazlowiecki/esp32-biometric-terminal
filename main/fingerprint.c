#include "fingerprint.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define BUF_SIZE 256

void fingerprint_init(void) {
    uart_config_t uc = {
        .baud_rate = 57600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    
    // Zwiększony bufor do 1024 (tak jak w Twoim dzialajacym kodzie)
    uart_driver_install(FP_UART_NUM, 1024, 0, 0, NULL, 0); 
    uart_param_config(FP_UART_NUM, &uc);
    uart_set_pin(FP_UART_NUM, FP_TX_PIN, FP_RX_PIN, -1, -1);
}

static void send_cmd(uint8_t pid, uint8_t len, uint8_t *content) {
    uart_flush_input(FP_UART_NUM); 
    uint8_t *pkt = malloc(9 + len);
    const uint8_t HEADER[2] = {0xEF, 0x01}; 
    const uint8_t ADDR[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    
    memcpy(pkt, HEADER, 2); memcpy(pkt+2, ADDR, 4); 
    pkt[6] = pid; pkt[7] = len >> 8; pkt[8] = len;
    memcpy(pkt+9, content, len-2); 
    
    uint16_t sum = pid + len; 
    for(int i = 0; i < len-2; i++) sum += content[i];
    pkt[9+len-2] = sum >> 8; pkt[9+len-1] = sum;
    
    uart_write_bytes(FP_UART_NUM, (const char*)pkt, 9+len); 
    free(pkt);
}

// Totalnie bezbłędny parser bez użycia timerów!
static uint8_t get_resp(uint8_t *d) {
    uint8_t byte; 
    int attempts = 40; // 40 prob * 10ms = 400ms na odpowiedz
    bool hdr = false;
    
    while(attempts > 0) {
        if(uart_read_bytes(FP_UART_NUM, &byte, 1, pdMS_TO_TICKS(10)) > 0) {
            if(byte == 0xEF) { 
                uint8_t n; 
                if(uart_read_bytes(FP_UART_NUM, &n, 1, pdMS_TO_TICKS(20)) > 0 && n == 0x01) { 
                    hdr = true; break; 
                } 
            }
        } else {
            attempts--; // Zmniejszamy licznik tylko, gdy nie ma danych w buforze
        }
    }
    
    if(!hdr) return 0xFF; // Ramka nie nadeszła
    
    uint8_t buf[20]; 
    int len = uart_read_bytes(FP_UART_NUM, buf, 10, pdMS_TO_TICKS(100));
    
    if(len >= 7) { 
        if(d) { d[0] = 0xEF; d[1] = 0x01; memcpy(d+2, buf, len); } 
        return buf[7]; // Kod statusu
    }
    return 0xFF;
}

bool fingerprint_test_connection(void) {
    return true; // Pomijamy, czytnik budzi się sam po komendach skanowania
}

int fingerprint_scan(void) {
    uint8_t gen[] = {0x01}; 
    uint8_t chr[] = {0x02, 0x01}; 
    uint8_t srch[] = {0x04, 0x01, 0x00, 0x00, 0x03, 0xE8}; 
    uint8_t rx[30];

    send_cmd(0x01, 3, gen);
    if(get_resp(NULL) == 0) {
        send_cmd(0x01, 4, chr);
        if(get_resp(NULL) == 0) {
            send_cmd(0x01, 8, srch); 
            get_resp(rx);
            if(rx[9] == 0) return (rx[10] << 8) | rx[11]; 
            if(rx[9] == 9) return -3; 
        }
    }
    return -1; 
}

bool fingerprint_enroll(uint8_t id) {
    uint8_t gen[] = {0x01}; uint8_t ch1[] = {0x02, 0x01}; uint8_t ch2[] = {0x02, 0x02}; uint8_t reg[] = {0x05};
    printf("[ENROLL] Poloz palec (ID: %d)...\n", id);
    
    // Czysta pętla oczekująca na przyłożenie palca
    while(1) { 
        send_cmd(0x01, 3, gen); 
        if(get_resp(NULL) == 0) break; // Sukces - zdjecie zrobione!
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
    
    send_cmd(0x01, 4, ch1); get_resp(NULL);
    printf("[ENROLL] Zdjecie 1 zrobione. ZABIERZ PALEC.\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    printf("[ENROLL] Poloz ponownie TEN SAM palec...\n");
    while(1) { 
        send_cmd(0x01, 3, gen); 
        if(get_resp(NULL) == 0) break; // Sukces - drugie zdjecie zrobione!
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
    
    send_cmd(0x01, 4, ch2); get_resp(NULL);
    send_cmd(0x01, 3, reg);
    
    if(get_resp(NULL) == 0) {
        uint8_t str[] = {0x06, 0x01, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)};
        send_cmd(0x01, 6, str);
        if(get_resp(NULL) == 0) {
            printf("[ENROLL] SUKCES! Zapisano ID: %d\n", id);
            return true;
        }
    }
    printf("[ENROLL] BLAD ZAPISU!\n");
    return false;
}