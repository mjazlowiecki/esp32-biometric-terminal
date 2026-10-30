#include "oled.h"

#include <string.h>

#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/task.h"
#include "i2c_bus.h"

static const char *TAG = "oled";

#define OLED_ADDR       0x3C
#define OLED_SPEED_HZ   400000
#define OLED_TIMEOUT_MS 50
#define OLED_TX_BUF_LEN 32

static i2c_master_dev_handle_t s_dev;
static u8g2_t s_u8g2;
static uint8_t s_tx_buf[OLED_TX_BUF_LEN];
static size_t s_tx_len;

static uint8_t oled_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    switch (msg)
    {
        case U8X8_MSG_BYTE_INIT:
            return 1;
        case U8X8_MSG_BYTE_START_TRANSFER:
            s_tx_len = 0;
            return 1;
        case U8X8_MSG_BYTE_SEND:
            if (s_tx_len + arg_int > sizeof(s_tx_buf))
            {
                return 0;
            }
            memcpy(&s_tx_buf[s_tx_len], arg_ptr, arg_int);
            s_tx_len += arg_int;
            return 1;
        case U8X8_MSG_BYTE_END_TRANSFER:
            return i2c_master_transmit(s_dev, s_tx_buf, s_tx_len, OLED_TIMEOUT_MS) == ESP_OK;
        default:
            return 0;
    }
}

static uint8_t oled_gpio_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;
    if (msg == U8X8_MSG_DELAY_MILLI)
    {
        vTaskDelay(pdMS_TO_TICKS(arg_int) + 1);
    }
    return 1;
}

esp_err_t oled_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OLED_ADDR,
        .scl_speed_hz    = OLED_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus_get(), &cfg, &s_dev), TAG, "add_device");

    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(&s_u8g2, U8G2_R0, oled_byte_cb, oled_gpio_delay_cb);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SendBuffer(&s_u8g2);
    return ESP_OK;
}

u8g2_t *oled_get(void)
{
    return &s_u8g2;
}