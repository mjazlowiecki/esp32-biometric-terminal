#include "i2c_bus.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";

#define I2C_BUS_PORT        I2C_NUM_0
#define I2C_BUS_SDA         GPIO_NUM_21
#define I2C_BUS_SCL         GPIO_NUM_22
#define I2C_SCAN_TIMEOUT_MS 50

static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port                     = I2C_BUS_PORT,
        .sda_io_num                   = I2C_BUS_SDA,
        .scl_io_num                   = I2C_BUS_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c_new_master_bus");
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get(void)
{
    return s_bus;
}

void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "Skan magistrali I2C...");
    int found = 0;

    for (uint16_t addr = 0x08; addr < 0x78; addr++)
    {
        if (i2c_master_probe(s_bus, addr, I2C_SCAN_TIMEOUT_MS) == ESP_OK)
        {
            ESP_LOGI(TAG, "  urzadzenie pod adresem 0x%02X", addr);
            found++;
        }
    }

    ESP_LOGI(TAG, "Znaleziono: %d urzadzen I2C", found);
}
