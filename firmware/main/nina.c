#include "nina.h"
#include "http_util.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "nina";
#define DEFAULT_ARS "096710000000"          // Landkreis Aschaffenburg
#define POLL_INTERVAL_MS (10 * 60 * 1000)   // alle 10 min
#define PARSE_BUF 32768

static nina_data_t s_data;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;

static int sev_rank(const char *s)
{
    if (!s) return 0;
    if (strcmp(s, "Extreme") == 0)  return 4;
    if (strcmp(s, "Severe") == 0)   return 3;
    if (strcmp(s, "Moderate") == 0) return 2;
    if (strcmp(s, "Minor") == 0)    return 1;
    return 0;
}

static void poll_once(void)
{
    char ars[16]; config_get_str_def("nina_ars", ars, sizeof(ars), DEFAULT_ARS);
    char url[96];
    snprintf(url, sizeof(url), "https://warnung.bund.de/api31/dashboard/%s.json", ars);

    char *buf = heap_caps_malloc(PARSE_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(PARSE_BUF);
    if (!buf) return;

    int n = http_get(url, buf, PARSE_BUF);
    if (n <= 0) { free(buf); ESP_LOGW(TAG, "Abruf fehlgeschlagen (ARS %s)", ars); return; }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); ESP_LOGW(TAG, "kein JSON-Array"); return; }

    int count = 0, best = -1;
    char headline[96] = { 0 }, severity[16] = { 0 }, provider[12] = { 0 };
    cJSON *e;
    cJSON_ArrayForEach(e, root) {
        cJSON *payload = cJSON_GetObjectItem(e, "payload");
        cJSON *data = payload ? cJSON_GetObjectItem(payload, "data") : NULL;
        cJSON *mt = data ? cJSON_GetObjectItem(data, "msgType") : NULL;
        if (cJSON_IsString(mt) && strcmp(mt->valuestring, "Cancel") == 0) continue;  // Entwarnung
        count++;

        cJSON *sev = data ? cJSON_GetObjectItem(data, "severity") : NULL;
        int r = sev_rank(cJSON_IsString(sev) ? sev->valuestring : NULL);
        if (r > best) {
            best = r;
            severity[0] = provider[0] = '\0';
            if (cJSON_IsString(sev)) strncpy(severity, sev->valuestring, sizeof(severity) - 1);
            cJSON *prov = data ? cJSON_GetObjectItem(data, "provider") : NULL;
            if (cJSON_IsString(prov)) strncpy(provider, prov->valuestring, sizeof(provider) - 1);
            // Titel: i18nTitle.de bevorzugt, sonst data.headline
            cJSON *i18 = cJSON_GetObjectItem(e, "i18nTitle");
            cJSON *de = i18 ? cJSON_GetObjectItem(i18, "de") : NULL;
            cJSON *hl = data ? cJSON_GetObjectItem(data, "headline") : NULL;
            const char *h = cJSON_IsString(de) ? de->valuestring
                          : (cJSON_IsString(hl) ? hl->valuestring : "");
            strncpy(headline, h, sizeof(headline) - 1);
        }
    }
    cJSON_Delete(root);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.valid = true;
    s_data.count = count;
    strncpy(s_data.headline, headline, sizeof(s_data.headline) - 1); s_data.headline[sizeof(s_data.headline)-1]='\0';
    strncpy(s_data.severity, severity, sizeof(s_data.severity) - 1); s_data.severity[sizeof(s_data.severity)-1]='\0';
    strncpy(s_data.provider, provider, sizeof(s_data.provider) - 1); s_data.provider[sizeof(s_data.provider)-1]='\0';
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d Warnung(en) (ARS %s)", count, ars);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(7000));
    for (;;) {
        poll_once();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_INTERVAL_MS));  // frueher bei Refresh
    }
}

void nina_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "nina", 7168, NULL, 3, &s_task);
}

void nina_get(nina_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}

void nina_refresh(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
