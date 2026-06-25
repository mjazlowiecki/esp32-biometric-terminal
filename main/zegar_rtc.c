#include "zegar_rtc.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include <stdio.h>

#define RTC_ADDR 0x68

static i2c_port_t active_i2c_port = I2C_NUM_MAX;

static uint8_t bcd2dec(uint8_t val) {
    return ((val / 16 * 10) + (val % 16));
}

void zegar_rtc_init(void) {
    printf("[RTC] Szukam zegara na magistrali zainicjowanej przez ekran OLED...\n");
    
    uint8_t reg = 0x00;
    uint8_t data[1];
    
    // U8g2 w ESP-IDF domyslnie używa portu I2C_NUM_0. Pukamy pod adres 0x68.
    esp_err_t ret = i2c_master_write_read_device(I2C_NUM_0, RTC_ADDR, &reg, 1, data, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        printf("[RTC] SUKCES! Znalazlem RTC (0x68) na porcie I2C_NUM_0.\n");
        active_i2c_port = I2C_NUM_0;
        return;
    }
    
    // Na wypadek, gdyby jednak u8g2 uzywalo portu 1
    ret = i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, data, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        printf("[RTC] SUKCES! Znalazlem RTC (0x68) na porcie I2C_NUM_1.\n");
        active_i2c_port = I2C_NUM_1;
        return;
    }

    printf("[RTC] BLAD! Komunikacja z RTC zawiodla. Czy kable SDA/SCL i zasilanie sa ok?\n");
}

void rtc_get_timestamp(char *buffer, size_t max_len) {
    if (active_i2c_port == I2C_NUM_MAX) {
        snprintf(buffer, max_len, "BRAK_KOMUNIKACJI_RTC");
        return;
    }

    uint8_t reg = 0x00;
    uint8_t data[7];
    
    // Czytamy czas z poprawnie zidentyfikowanego portu
    esp_err_t ret = i2c_master_write_read_device(active_i2c_port, RTC_ADDR, &reg, 1, data, 7, pdMS_TO_TICKS(100));
    
    if (ret == ESP_OK) {
        int sec = bcd2dec(data[0] & 0x7F);
        int min = bcd2dec(data[1]);
        int hour = bcd2dec(data[2] & 0x3F);
        int day = bcd2dec(data[4]);
        int month = bcd2dec(data[5]);
        int year = bcd2dec(data[6]) + 2000;
        
        snprintf(buffer, max_len, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, min, sec);
    } else {
        snprintf(buffer, max_len, "BLAD_ODCZYTU_RTC");
    }
}