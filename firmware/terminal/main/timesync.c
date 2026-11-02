#include "timesync.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "ds3231.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "timesync";

// Polska: CET (UTC+1) zimą, CEST (UTC+2) od ostatniej niedzieli marca
// do ostatniej niedzieli października
#define TIMESYNC_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

#define TIMESYNC_NTP_SERVER "pool.ntp.org"

static bool s_valid;
static atomic_bool s_sntp_synced;

esp_err_t timesync_init(void)
{
    struct tm rtc;
    ESP_RETURN_ON_ERROR(ds3231_read_time(&rtc, &s_valid), TAG, "odczyt RTC");

    // RTC trzyma UTC. Strefa TZ nie jest jeszcze ustawiona (domyslnie UTC),
    // wiec mktime() traktuje strukture jako UTC. Kolejnosc ma znaczenie
    time_t utc        = mktime(&rtc);
    struct timeval tv = {.tv_sec = utc};
    settimeofday(&tv, NULL);

    setenv("TZ", TIMESYNC_TZ, 1);
    tzset();

    ESP_LOGI(TAG, "Czas systemowy z RTC%s", s_valid ? "" : "(niewiarygodny: OSF)");
    return ESP_OK;
}

static void timesync_sntp_cb(struct timeval *tv)
{
    (void)tv;
    atomic_store(&s_sntp_synced, true);
}

esp_err_t timesync_start_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(TIMESYNC_NTP_SERVER);
    cfg.sync_cb           = timesync_sntp_cb;
    return esp_netif_sntp_init(&cfg);
}

void timesync_poll(void)
{
    if (!atomic_exchange(&s_sntp_synced, false))
    {
        return;
    }

    s_valid = true;

    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    if (ds3231_set_time(&utc) == ESP_OK)
    {
        ESP_LOGI(TAG, "SNTP: czas zsynchronizowany, zapisano do RTC");
    }
    else
    {
        ESP_LOGW(TAG, "SNTP: Czas zsynchonizowany, ale zapis do RTC nieudany");
    }
}

bool timesync_is_valid(void)
{
    return s_valid;
}