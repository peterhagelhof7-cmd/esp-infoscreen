#include "owm.h"
#include "http_util.h"
#include "config_store.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "owm";
// Johannesberg (lat/lon), metrisch, deutsche Beschreibung.
#define OWM_URL "https://api.openweathermap.org/data/2.5/weather?lat=50.008&lon=9.216&units=metric&lang=de&appid=%s"
#define POLL_INTERVAL_MS (20 * 60 * 1000)   // 20 min -> 72 Abfragen/Tag (< 100/Tag)

static owm_data_t s_data;
static SemaphoreHandle_t s_lock;

static int iround(double v) { return (int)(v >= 0 ? v + 0.5 : v - 0.5); }

static void poll_once(void)
{
    owm_data_t d = { 0 };

    char key[40];
    if (!config_get_str("owm_key", key, sizeof(key)) || key[0] == '\0') {
        d.has_key = false;
        xSemaphoreTake(s_lock, portMAX_DELAY); s_data = d; xSemaphoreGive(s_lock);
        return;
    }
    d.has_key = true;

    char url[256];
    snprintf(url, sizeof(url), OWM_URL, key);

    static char buf[4096];
    int n = http_get(url, buf, sizeof(buf));
    if (n > 0) {
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            cJSON *main = cJSON_GetObjectItem(root, "main");
            cJSON *wind = cJSON_GetObjectItem(root, "wind");
            cJSON *weather = cJSON_GetObjectItem(root, "weather");
            if (cJSON_IsObject(main)) {
                cJSON *t = cJSON_GetObjectItem(main, "temp");
                cJSON *f = cJSON_GetObjectItem(main, "feels_like");
                cJSON *h = cJSON_GetObjectItem(main, "humidity");
                if (cJSON_IsNumber(t)) d.temp = iround(t->valuedouble);
                if (cJSON_IsNumber(f)) d.feels = iround(f->valuedouble);
                if (cJSON_IsNumber(h)) d.humidity = h->valueint;
                d.valid = true;
            }
            if (cJSON_IsArray(weather) && cJSON_GetArraySize(weather) > 0) {
                cJSON *w0 = cJSON_GetArrayItem(weather, 0);
                cJSON *desc = cJSON_GetObjectItem(w0, "description");
                if (cJSON_IsString(desc)) strncpy(d.desc, desc->valuestring, sizeof(d.desc) - 1);
            }
            if (cJSON_IsObject(wind)) {
                cJSON *sp = cJSON_GetObjectItem(wind, "speed");   // m/s
                if (cJSON_IsNumber(sp)) d.wind_kmh = iround(sp->valuedouble * 3.6);
            }
            cJSON_Delete(root);
        }
    }
    if (!d.valid) ESP_LOGW(TAG, "Abfrage fehlgeschlagen (Key/Netz?)");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = d;
    xSemaphoreGive(s_lock);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void owm_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "owm", 8192, NULL, 3, NULL);
}

void owm_get(owm_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
