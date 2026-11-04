#include "sdcard.h"

#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";

#define SD_SPI_HOST SPI3_HOST // VSPI: natywne piny 23/19/18/5
#define SD_PIN_MOSI GPIO_NUM_23
#define SD_PIN_MISO GPIO_NUM_19
#define SD_PIN_CLK  GPIO_NUM_18
#define SD_PIN_CS   GPIO_NUM_5

// Na plytce stykowej 20 MHz (domyslne) daje bledy CRC - sdmmc_check_scr
#define SD_MAX_FREQ_KHZ 5000

static sdmmc_card_t *s_card;

esp_err_t sdcard_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num     = SD_PIN_MOSI,
        .miso_io_num     = SD_PIN_MISO,
        .sclk_io_num     = SD_PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SD_SPI_HOST, &bus, SDSPI_DEFAULT_DMA), TAG,
                        "spi_bus_initialize");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot         = SD_SPI_HOST;
    host.max_freq_khz = SD_MAX_FREQ_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs               = SD_PIN_CS;
    slot.host_id               = SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot, &mount, &s_card),
                        TAG, "mount");

    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}