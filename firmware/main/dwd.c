#include "dwd.h"
#include "http_util.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "dwd";
// Bright Sky (DWD) Warnungen fuer Johannesberg (lat/lon)
#define DWD_URL "https://api.brightsky.dev/alerts?lat=50.008&lon=9.216"
#define POLL_INTERVAL_MS (15 * 60 * 1000)   // alle 15 min

static dwd_data_t s_data;
static SemaphoreHandle_t s_lock;

static void poll_once(void)
{
    static char buf[8192];
    dwd_data_t d = { 0 };

    int n = http_get(DWD_URL, buf, sizeof(buf));
    if (n > 0) {
        cJSON *root = cJSON_Parse(buf);
        cJSON *alerts = root ? cJSON_GetObjectItem(root, "alerts") : NULL;
        if (cJSON_IsArray(alerts)) {
            d.valid = true;
            d.count = cJSON_GetArraySize(alerts);
            if (d.count > 0) {
                cJSON *a = cJSON_GetArrayItem(alerts, 0);
                cJSON *hl = cJSON_GetObjectItem(a, "headline_de");
                cJSON *sv = cJSON_GetObjectItem(a, "severity");
                if (cJSON_IsString(hl)) strncpy(d.headline, hl->valuestring, sizeof(d.headline) - 1);
                if (cJSON_IsString(sv)) strncpy(d.severity, sv->valuestring, sizeof(d.severity) - 1);
            }
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGW(TAG, "Abruf fehlgeschlagen");
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = d;
    xSemaphoreGive(s_lock);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(6000));
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void dwd_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "dwd", 7168, NULL, 3, NULL);
}

void dwd_get(dwd_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
