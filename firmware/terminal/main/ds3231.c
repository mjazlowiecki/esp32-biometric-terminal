#include "ds3231.h"

#include "esp_check.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "string.h"

static const char *TAG = "ds3231";

#define DS3231_ADDR       0x68
#define DS3231_SPEED_HZ   100000
#define DS3231_TIMEOUT_MS 50
#define DS3231_REG_TIME   0x00
#define DS3231_REG_STATUS 0x0F
#define DS3231_STATUS_OSF 0X80

static i2c_master_dev_handle_t s_dev;

static esp_err_t ds3231_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, DS3231_TIMEOUT_MS);
}

static esp_err_t ds3231_write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[8];
    ESP_RETURN_ON_FALSE(len < sizeof(buf), ESP_ERR_INVALID_SIZE, TAG, "za duzo danych");

    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, DS3231_TIMEOUT_MS);
}

static uint8_t bin_to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int bcd_to_bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

esp_err_t ds3231_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = DS3231_ADDR,
        .scl_speed_hz    = DS3231_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus_get(), &cfg, &s_dev), TAG, "add_device");
    return ESP_OK;
}

esp_err_t ds3231_read_time(struct tm *out, bool *valid)
{
    uint8_t r[7];
    ESP_RETURN_ON_ERROR(ds3231_read_regs(DS3231_REG_TIME, r, sizeof(r)), TAG, "read_time");

    uint8_t status;
    ESP_RETURN_ON_ERROR(ds3231_read_regs(DS3231_REG_STATUS, &status, 1), TAG, "read_status");

    // niektóre z rejestrów to nie cyfry tylko flagi  -> maski
    *out = (struct tm){
        .tm_sec  = bcd_to_bin(r[0] & 0x7F),
        .tm_min  = bcd_to_bin(r[1] & 0x7F),
        .tm_hour = bcd_to_bin(r[2] & 0x3F),
        .tm_mday = bcd_to_bin(r[4] & 0x3F),
        .tm_mon  = bcd_to_bin(r[5] & 0x1F) - 1,
        .tm_year = bcd_to_bin(r[6]) + 100,
    };
    *valid = (status & DS3231_STATUS_OSF) == 0;
    return ESP_OK;
}

esp_err_t ds3231_set_time(const struct tm *t)
{
    ESP_RETURN_ON_FALSE(t->tm_year >= 100 && t->tm_year < 200, ESP_ERR_INVALID_ARG, TAG,
                        "rok poza zakresem 2000-2099");

    uint8_t r[7] = {
        bin_to_bcd(t->tm_sec),        bin_to_bcd(t->tm_min),  bin_to_bcd(t->tm_hour),
        (uint8_t)(t->tm_wday + 1),    bin_to_bcd(t->tm_mday), bin_to_bcd(t->tm_mon + 1),
        bin_to_bcd(t->tm_year - 100),
    };
    ESP_RETURN_ON_ERROR(ds3231_write_regs(DS3231_REG_TIME, r, sizeof(r)), TAG, "write_time");

    uint8_t status;
    ESP_RETURN_ON_ERROR(ds3231_read_regs(DS3231_REG_STATUS, &status, 1), TAG, "read status");
    status &= (uint8_t)~DS3231_STATUS_OSF;
    return ds3231_write_regs(DS3231_REG_STATUS, &status, 1);
}