#include "kurse.h"
#include "http_util.h"

#include <string.h>
#include <stdlib.h>   // atof
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR
#include "cJSON.h"

static const char *TAG = "kurse";

// Quelle: Bitstamp Public-Ticker. Gruende gegen Coinbase/CoinGecko:
//  - CoinGecko drosselte die Geraete-IP mit HTTP 429.
//  - Coinbase/Kraken/Frankfurter nutzen Google-Trust-Services-Zertifikate, die
//    das ESP-IDF-crt_bundle auf dem Geraet NICHT verifizieren kann
//    ("No matching trusted root", mbedtls -0x3000).
// Bitstamp nutzt DigiCert (vom Bundle vertraut, wie stroeer/owm/brightsky) und
// liefert je Ticker last + open_24 (Kurs vor 24h) + percent_change_24 -> echte
// 24h-Aenderung ohne Selbst-Tracking. Basis Euro: BTC-EUR direkt, EUR-USD als
// BTC-Cross (btcusd/btceur, BTC kuerzt sich raus).
#define BTCEUR_URL "https://www.bitstamp.net/api/v2/ticker/btceur/"
#define BTCUSD_URL "https://www.bitstamp.net/api/v2/ticker/btcusd/"
#define POLL_INTERVAL_MS (5 * 60 * 1000)   // alle 5 min

static kurse_data_t s_data;
static SemaphoreHandle_t s_lock;

// Einen Bitstamp-Ticker holen: last, open_24, percent_change_24 (alles Strings).
static bool fetch_ticker(const char *url, double *last, double *open24, double *pct)
{
    static EXT_RAM_BSS_ATTR char buf[1024];
    int n = http_get(url, buf, sizeof(buf));
    if (n <= 0) return false;
    cJSON *root = cJSON_Parse(buf);
    if (!root) return false;
    cJSON *jl = cJSON_GetObjectItem(root, "last");
    cJSON *jo = cJSON_GetObjectItem(root, "open_24");
    cJSON *jp = cJSON_GetObjectItem(root, "percent_change_24");
    bool ok = cJSON_IsString(jl) && cJSON_IsString(jo);
    if (ok) {
        *last   = atof(jl->valuestring);
        *open24 = atof(jo->valuestring);
        *pct    = cJSON_IsString(jp) ? atof(jp->valuestring) : 0.0;
    }
    cJSON_Delete(root);
    return ok && *last > 0.0 && *open24 > 0.0;
}

static void poll_once(void)
{
    double e_last, e_open, e_pct, u_last, u_open, u_pct;
    if (!fetch_ticker(BTCEUR_URL, &e_last, &e_open, &e_pct)) { ESP_LOGW(TAG, "BTC-EUR-Abruf fehlgeschlagen"); return; }
    if (!fetch_ticker(BTCUSD_URL, &u_last, &u_open, &u_pct)) { ESP_LOGW(TAG, "BTC-USD-Abruf fehlgeschlagen"); return; }

    kurse_data_t d = { 0 };
    d.valid     = true;
    d.btc_eur   = e_last;               // 1 BTC in EUR
    d.btc_chg   = e_pct;                // echte 24h-Aenderung BTC-EUR (Bitstamp)
    d.eur_usd   = u_last / e_last;      // BTC-Cross -> 1 EUR in USD
    double eur_usd_24 = u_open / e_open;                 // Cross vor 24h
    d.eur_chg   = eur_usd_24 > 0.0 ? (d.eur_usd - eur_usd_24) / eur_usd_24 * 100.0 : 0.0;
    d.has_trend = true;                 // 24h-Werte liegen sofort vor

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = d;   // nur gueltige Werte uebernehmen -> letzte gute bleiben bei Fehlern
    xSemaphoreGive(s_lock);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(9000));
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void kurse_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "kurse", 6144, NULL, 3, NULL);
}

void kurse_get(kurse_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
