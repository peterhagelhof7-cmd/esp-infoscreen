#include "muell.h"
#include "http_util.h"
#include "time_sync.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "muell";
// Johannesberg-Oberafferbach (Landkreis Aschaffenburg, MyMuell/jumomind)
#define MUELL_URL "https://mymuell.jumomind.com/mmapp/api.php?r=dates&city_id=44886&area_id=44886"
#define MAX_ENTRIES 16
#define POLL_INTERVAL_MS (6 * 60 * 60 * 1000)   // alle 6 Stunden

static muell_entry_t s_list[MAX_ENTRIES];
static int s_count;
static SemaphoreHandle_t s_lock;

static void poll_once(void)
{
    char today[11];
    time_today_str(today, sizeof(today));

    char *buf = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(16384);
    if (!buf) return;

    int n = http_get(MUELL_URL, buf, 16384);
    if (n <= 0) { free(buf); ESP_LOGW(TAG, "Abruf fehlgeschlagen"); return; }

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(arr)) { cJSON_Delete(arr); ESP_LOGW(TAG, "kein JSON-Array"); return; }

    muell_entry_t tmp[MAX_ENTRIES];
    int count = 0;
    cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        cJSON *day = cJSON_GetObjectItem(e, "day");
        cJSON *title = cJSON_GetObjectItem(e, "title");
        if (!cJSON_IsString(day)) continue;
        // nur heutige/zukuenftige Termine (ISO-Datum -> Stringvergleich)
        if (today[0] && strcmp(day->valuestring, today) < 0) continue;
        strncpy(tmp[count].day, day->valuestring, sizeof(tmp[0].day) - 1);
        tmp[count].day[sizeof(tmp[0].day) - 1] = '\0';
        tmp[count].title[0] = '\0';
        if (cJSON_IsString(title)) {
            strncpy(tmp[count].title, title->valuestring, sizeof(tmp[0].title) - 1);
            tmp[count].title[sizeof(tmp[0].title) - 1] = '\0';
        }
        if (++count >= MAX_ENTRIES) break;
    }
    cJSON_Delete(arr);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_list, tmp, sizeof(tmp));
    s_count = count;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d Termine geladen", count);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));   // kurz warten (Netz/Uhr)
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void muell_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(poll_task, "muell", 8192, NULL, 3, NULL);
}

int muell_get(muell_entry_t *out, int max)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count < max ? s_count : max;
    memcpy(out, s_list, n * sizeof(muell_entry_t));
    xSemaphoreGive(s_lock);
    return n;
}
