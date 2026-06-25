idf_component_register(SRCS "zegar_rtc.c" "sdcard.c" "fingerprint.c" "signal.c" "ui.c" "main.c" "wifi_mqtt.c"
                       INCLUDE_DIRS "."
                       REQUIRES u8g2-hal-esp-idf fatfs sdmmc nvs_flash esp_wifi mqtt)