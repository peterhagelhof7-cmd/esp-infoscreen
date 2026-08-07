#include "time_sync.h"

#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
#include "esp_netif_sntp.h"
#include "esp_log.h"

static const char *TAG = "time";

static void on_sync(struct timeval *tv)
{
    (void)tv;
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    ESP_LOGI(TAG, "Zeit synchronisiert: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void time_sync_init(void)
{
    // Zeitzone Deutschland: MEZ/MESZ inkl. Sommerzeit-Umschaltung.
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    // Statischer Server pool.ntp.org. KEIN server_from_dhcp: das liess den
    // Client sonst auf einen DHCP-gelieferten NTP-Server warten und den
    // statischen Server ignorieren -> es wurde nie eine DNS-Abfrage gestartet.
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;                 // synchronisiert, sobald das Netz da ist
    cfg.sync_cb = on_sync;            // loggt den ersten erfolgreichen Sync
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

void time_today_str(char *out, size_t len)
{
    if (!time_sync_is_valid()) { if (len) out[0] = '\0'; return; }
    time_t now = 0; time(&now);
    struct tm tm; localtime_r(&now, &tm);
    snprintf(out, len, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}
