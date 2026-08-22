#include "kurse.h"
#include "http_util.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR
#include "cJSON.h"

static const char *TAG = "kurse";

// CoinGecko Simple-Price: BTC in USD und EUR mit 24h-Aenderung, ein Aufruf.
#define KURSE_URL \
    "https://api.coingecko.com/api/v3/simple/price" \
    "?ids=bitcoin&vs_currencies=usd,eur&include_24hr_change=true"
#define POLL_INTERVAL_MS (5 * 60 * 1000)   // alle 5 min (288/Tag, unkritisch)

static kurse_data_t s_data;
static SemaphoreHandle_t s_lock;

static void poll_once(void)
{
    static EXT_RAM_BSS_ATTR char buf[2048];   // Parse-Puffer ins PSRAM
    int n = http_get(KURSE_URL, buf, sizeof(buf));
    if (n <= 0) { ESP_LOGW(TAG, "Abruf fehlgeschlagen"); return; }

    kurse_data_t d = { 0 };
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *btc = cJSON_GetObjectItem(root, "bitcoin");
        cJSON *ju = btc ? cJSON_GetObjectItem(btc, "usd") : NULL;
        cJSON *je = btc ? cJSON_GetObjectItem(btc, "eur") : NULL;
        cJSON *cu = btc ? cJSON_GetObjectItem(btc, "usd_24h_change") : NULL;
        cJSON *ce = btc ? cJSON_GetObjectItem(btc, "eur_24h_change") : NULL;
        if (cJSON_IsNumber(ju) && cJSON_IsNumber(je) && je->valuedouble > 0.0) {
            double du = cJSON_IsNumber(cu) ? cu->valuedouble : 0.0;
            double de = cJSON_IsNumber(ce) ? ce->valuedouble : 0.0;
            d.btc_usd = ju->valuedouble;
            d.btc_chg = du;
            d.eur_usd = ju->valuedouble / je->valuedouble;   // EUR-USD ueber BTC
            d.eur_chg = du - de;                              // Cross-Aenderung ~ Differenz
            d.valid = true;
        }
        cJSON_Delete(root);
    }
    if (!d.valid) { ESP_LOGW(TAG, "Parsen fehlgeschlagen"); return; }

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
