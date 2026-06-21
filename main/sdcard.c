#include "sdcard.h"
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
        .format_if_mount_failed = true, // Jesli karta jest nowa, automatycznie sformatuje na FAT32
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // KLUCZOWE DLA STYKOWKI: Niska czestotliwosc 400kHz zapobiega zakloceniom na kablach
    host.max_freq_khz = 400; 
    
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
        printf("[SD] Blad montowania karty MicroSD. Sprawdz kable i zasilanie!\n");
        return;
    }

    printf("[SD] SUKCES! Karta MicroSD zamontowana poprawnie.\n");
    sdmmc_card_print_info(stdout, card); // Wypisze rozmiar i typ karty w konsoli
    is_initialized = true;
}

void sdcard_log_activity(uint8_t user_id, const char* activity_name, const char* action) {
    if (!is_initialized) {
        printf("[SD] Karta niezainicjalizowana, pomijam zapis bufora.\n");
        return;
    }
    
    // Otwieramy plik w trybie "a" (append - dopisywanie na koncu pliku)
    FILE *f = fopen("/sdcard/dane_rcp.csv", "a");
    if (f == NULL) {
        printf("[SD] Blad otwarcia pliku dane_rcp.csv!\n");
        return;
    }
    
    // Format CSV: ID_Uzytkownika;Nazwa_Aktywnosci;Akcja;Timestamp
    // Zegar RTC podepniemy tu w kolejnym etapie, na razie wpisujemy zaslepke
    fprintf(f, "%d;%s;%s;BRAK_ZEGARA\n", user_id, activity_name, action);
    fclose(f);
    
    printf("[SD] BUFOR OFFLINE: Zapisano do pliku CSV -> Uzytkownik: %d | Aktywnosc: %s | Akcja: %s\n", user_id, activity_name, action);
}