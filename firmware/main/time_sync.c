#include "time_sync.h"

#include <time.h>
#include <stdlib.h>
#include "esp_netif_sntp.h"
#include "esp_log.h"

static const char *TAG = "time";

void time_sync_init(void)
{
    // Zeitzone Deutschland: MEZ/MESZ inkl. Sommerzeit-Umschaltung.
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;                 // startet, synchronisiert sobald Netz da ist
    cfg.server_from_dhcp = true;      // falls Router einen NTP-Server anbietet
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) ESP_LOGW(TAG, "SNTP-Init: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "NTP gestartet (pool.ntp.org, TZ Deutschland)");
}

bool time_sync_is_valid(void)
{
    time_t now = 0;
    time(&now);
    struct tm tm;
    localtime_r(&now, &tm);
    return (tm.tm_year + 1900) >= 2021;   // vor Sync steht die Uhr auf 1970
}
