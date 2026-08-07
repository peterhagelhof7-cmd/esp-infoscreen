#include "termine.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "termine";
#define KEY "termine"
#define BUFSZ 4000        // NVS-String-Obergrenze (~4000 B)
#define MAX_TERMINE 50    // selbst angelegte Termine (Muell zaehlt NICHT dazu)

// Laedt das gespeicherte JSON-Array (oder ein leeres).
static cJSON *load_array(void)
{
    char *buf = malloc(BUFSZ);
    if (!buf) return cJSON_CreateArray();
    cJSON *arr = NULL;
    if (config_get_str(KEY, buf, BUFSZ)) arr = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(arr)) { cJSON_Delete(arr); arr = cJSON_CreateArray(); }
    return arr;
}

// Speichert das Array. Liefert false, wenn es nicht in den NVS-String passt.
static bool save_array(cJSON *arr)
{
    char *json = cJSON_PrintUnformatted(arr);
    if (!json) return false;
    bool ok = strlen(json) < BUFSZ;
    if (ok) config_set_str(KEY, json);
    else ESP_LOGW(TAG, "Termine-JSON zu gross (%u B), nicht gespeichert", (unsigned)strlen(json));
    cJSON_free(json);
    return ok;
}

int termine_get_all(termine_entry_t *out, int max)
{
    cJSON *arr = load_array();
    int n = 0;
    cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        if (n >= max) break;
        cJSON *d = cJSON_GetObjectItem(e, "d");
        cJSON *t = cJSON_GetObjectItem(e, "t");
        cJSON *ti = cJSON_GetObjectItem(e, "ti");
        memset(&out[n], 0, sizeof(out[n]));
        if (cJSON_IsString(d))  strncpy(out[n].date, d->valuestring, sizeof(out[0].date) - 1);
        if (cJSON_IsString(t))  strncpy(out[n].time, t->valuestring, sizeof(out[0].time) - 1);
        if (cJSON_IsString(ti)) strncpy(out[n].title, ti->valuestring, sizeof(out[0].title) - 1);
        n++;
    }
    cJSON_Delete(arr);
    return n;
}

bool termine_add(const char *date, const char *time, const char *title)
{
    if (!title || !title[0] || !date || !date[0]) return false;
    cJSON *arr = load_array();
    if (cJSON_GetArraySize(arr) >= MAX_TERMINE) { cJSON_Delete(arr); return false; }
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "d", date);
    cJSON_AddStringToObject(e, "t", time ? time : "");
    cJSON_AddStringToObject(e, "ti", title);
    cJSON_AddItemToArray(arr, e);
    bool ok = save_array(arr);   // false = passt nicht mehr in den NVS-Speicher
    cJSON_Delete(arr);
    if (ok) ESP_LOGI(TAG, "Termin hinzugefuegt: %s %s", date, title);
    return ok;
}

void termine_delete(int idx)
{
    cJSON *arr = load_array();
    if (idx >= 0 && idx < cJSON_GetArraySize(arr)) {
        cJSON_DeleteItemFromArray(arr, idx);
        save_array(arr);
    }
    cJSON_Delete(arr);
}

int termine_purge_past(void)
{
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_year <= 120) return 0;   // Zeit noch nicht synchronisiert
    char today[16];
    strftime(today, sizeof(today), "%Y-%m-%d", &tm);

    cJSON *arr = load_array();
    int removed = 0, i = 0;
    while (i < cJSON_GetArraySize(arr)) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        cJSON *d = cJSON_GetObjectItem(e, "d");
        if (cJSON_IsString(d) && strcmp(d->valuestring, today) < 0) {
            cJSON_DeleteItemFromArray(arr, i);   // vergangen -> raus (Index nicht erhoehen)
            removed++;
        } else {
            i++;
        }
    }
    if (removed) { save_array(arr); ESP_LOGI(TAG, "%d vergangene Termine geloescht", removed); }
    cJSON_Delete(arr);
    return removed;
}

static void purge_timer_cb(void *arg)
{
    (void)arg;
    termine_purge_past();
}

void termine_init(void)
{
    termine_purge_past();   // sofort (falls Zeit schon da), sonst greift der Timer
    const esp_timer_create_args_t ta = { .callback = purge_timer_cb, .name = "termine_purge" };
    esp_timer_handle_t th;
    if (esp_timer_create(&ta, &th) == ESP_OK)
        esp_timer_start_periodic(th, (uint64_t)30 * 60 * 1000000);   // alle 30 min
}
