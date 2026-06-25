#include "sdcard.h"
#include "zegar_rtc.h" // KLUCZOWE: Dodajemy obsluge zegara
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static bool is_initialized = false;

void sdcard_init(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true, 
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 400; // Niska predkosc = wysoka niezawodnosc na stykowce
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        printf("[SD] Blad inicjalizacji magistrali SPI!\n");
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_CS;
    slot_config.host_id = host.slot;

    printf("[SD] Montowanie systemu plikow FAT...\n");
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        printf("[SD] Blad montowania karty MicroSD.\n");
        return;
    }

    printf("[SD] SUKCES! Karta MicroSD zamontowana poprawnie.\n");
    is_initialized = true;
}

void sdcard_log_activity(uint8_t user_id, const char* activity_name, const char* action) {
    if (!is_initialized) return;
    
    FILE *f = fopen("/sdcard/dane_rcp.csv", "a");
    if (f == NULL) {
        printf("[SD] Blad otwarcia pliku dane_rcp.csv!\n");
        return;
    }
    
    // --- MAGIA: Pobranie czasu z modulu RTC ---
    char timestamp[32];
    rtc_get_timestamp(timestamp, sizeof(timestamp));
    
    // Zapis z czasem (Timestamp;ID;Aktywnosc;Akcja)
    fprintf(f, "%s;%d;%s;%s\n", timestamp, user_id, activity_name, action);
    fclose(f);
    
    printf("[SD] Zapisano -> Czas: %s | ID: %d | Aktywnosc: %s | Akcja: %s\n", timestamp, user_id, activity_name, action);
}