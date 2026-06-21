#pragma once
#include <stdbool.h>
#include <stdint.h>


#define FP_TX_PIN GPIO_NUM_16
#define FP_RX_PIN GPIO_NUM_17
#define FP_UART_NUM UART_NUM_2

void fingerprint_init(void);
bool fingerprint_test_connection(void);
int fingerprint_scan(void);
bool fingerprint_enroll(uint8_t id);