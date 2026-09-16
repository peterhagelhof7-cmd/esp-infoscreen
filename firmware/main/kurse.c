#include "kurse.h"
#include "http_util.h"

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR
#include "cJSON.h"

static const char *TAG = "kurse";

// Coinbase exchange-rates: ein Aufruf liefert die Umrechnung von 1 EUR in alle
// Waehrungen inkl. USD und BTC. Keyless, kein aggressives Rate-Limit (anders als
// CoinGecko, das die Geraete-IP zeitweise mit HTTP 429 blockte).
#define KURSE_URL "https://api.coinbase.com/v2/exchange-rates?currency=EUR"
#define POLL_INTERVAL_MS (5 * 60 * 1000)   // alle 5 min

// Selbst gemerkte 24h-Historie: stuendliche Snapshots im PSRAM (RAM-only, nach
// Neustart leer). 26 Slots decken > 25h ab.
#define SNAP_MAX     26
#define SNAP_STEP_S  3600           // ein Snapshot pro Stunde
#define TREND_MIN_S  (23 * 3600)    // Baseline muss >= 23h alt sein
#define TREND_MAX_S  (25 * 3600)    // ... und <= 25h (sonst kein Trend)

typedef struct { time_t t; double btc_eur, eur_usd; } snap_t;
static EXT_RAM_BSS_ATTR snap_t s_snaps[SNAP_MAX];
static int    s_snap_n;
static time_t s_last_snap;

static kurse_data_t s_data;
static SemaphoreHandle_t s_lock;

static bool time_valid(time_t now)
{
    struct tm tm; localtime_r(&now, &tm);
    return tm.tm_year > 120;   // NTP gesetzt (Jahr > 2020)
}

static void push_snapshot(time_t now, double btc_eur, double eur_usd)
{
    if (s_snap_n == SNAP_MAX) {              // voll -> aeltesten verwerfen
        memmove(&s_snaps[0], &s_snaps[1], sizeof(snap_t) * (SNAP_MAX - 1));
        s_snap_n--;
    }
    s_snaps[s_snap_n].t = now;
    s_snaps[s_snap_n].btc_eur = btc_eur;
    s_snaps[s_snap_n].eur_usd = eur_usd;
    s_snap_n++;
    s_last_snap = now;
}

// Baseline: den Snapshot mit einem Alter moeglichst nahe an 24h waehlen (im
// Fenster 23..25h). Liefert false, wenn noch keine passende Historie da ist.
static bool baseline_24h(time_t now, const snap_t **out)
{
    const snap_t *best = NULL; time_t best_d = 0;
    for (int i = 0; i < s_snap_n; i++) {
        time_t age = now - s_snaps[i].t;
        if (age < TREND_MIN_S || age > TREND_MAX_S) continue;
        time_t d = age > 86400 ? age - 86400 : 86400 - age;
        if (!best || d < best_d) { best = &s_snaps[i]; best_d = d; }
    }
    if (best) { *out = best; return true; }
    return false;
}

static void poll_once(void)
{
    static EXT_RAM_BSS_ATTR char buf[8192];   // exchange-rates ist gross -> PSRAM
    int n = http_get(KURSE_URL, buf, sizeof(buf));
    if (n <= 0) { ESP_LOGW(TAG, "Abruf fehlgeschlagen"); return; }

    double eur_usd = 0.0, eur_btc = 0.0;
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *data  = cJSON_GetObjectItem(root, "data");
        cJSON *rates = data ? cJSON_GetObjectItem(data, "rates") : NULL;
        cJSON *ju = rates ? cJSON_GetObjectItem(rates, "USD") : NULL;
        cJSON *jb = rates ? cJSON_GetObjectItem(rates, "BTC") : NULL;
        // rates sind Strings ("1.147...") -> valuestring via atof.
        if (cJSON_IsString(ju)) eur_usd = atof(ju->valuestring);
        if (cJSON_IsString(jb)) eur_btc = atof(jb->valuestring);
        cJSON_Delete(root);
    }
    if (eur_usd <= 0.0 || eur_btc <= 0.0) { ESP_LOGW(TAG, "Parsen fehlgeschlagen"); return; }

    double btc_eur = 1.0 / eur_btc;   // 1 BTC in EUR

    kurse_data_t d = { 0 };
    d.valid   = true;
    d.btc_eur = btc_eur;
    d.eur_usd = eur_usd;

    time_t now = time(NULL);
    if (time_valid(now)) {
        if (s_last_snap == 0 || (now - s_last_snap) >= SNAP_STEP_S)
            push_snapshot(now, btc_eur, eur_usd);
        const snap_t *base;
        if (baseline_24h(now, &base) && base->btc_eur > 0.0 && base->eur_usd > 0.0) {
            d.has_trend = true;
            d.btc_chg = (btc_eur - base->btc_eur) / base->btc_eur * 100.0;
            d.eur_chg = (eur_usd - base->eur_usd) / base->eur_usd * 100.0;
        }
    }

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
    s_snap_n = 0; s_last_snap = 0;
    xTaskCreate(poll_task, "kurse", 6144, NULL, 3, NULL);
}

void kurse_get(kurse_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
