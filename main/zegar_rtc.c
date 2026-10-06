#include "zegar_rtc.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

void zegar_rtc_init(void) {
    printf("[RTC] Uruchamiam synchronizacje czasu przez WiFi (SNTP)...\n");
    
    // Konfiguracja pobierania czasu z internetu
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    // Ustawienie strefy czasowej dla Polski (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void rtc_get_timestamp(char *buffer, size_t max_len) {
    time_t now;
    struct tm timeinfo;
    
    // Pobranie aktualnego czasu z procesora
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Jesli rok jest mniejszy niz 2024, to znaczy ze WiFi jeszcze nie pobralo czasu
    if (timeinfo.tm_year < 124) {
        snprintf(buffer, max_len, "CZEKAM_NA_WIFI");
    } else {
        // Formatujemy czas, np. 2024-06-25 10:30:15
        strftime(buffer, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
}