#include "termine.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "termine";
#define KEY "termine"
#define BUFSZ 4000
#define MAX_TERMINE 40

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

static void save_array(cJSON *arr)
{
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        if (strlen(json) < BUFSZ) config_set_str(KEY, json);
        else ESP_LOGW(TAG, "Termine-JSON zu gross (%u), nicht gespeichert", (unsigned)strlen(json));
        cJSON_free(json);
    }
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
    save_array(arr);
    cJSON_Delete(arr);
    ESP_LOGI(TAG, "Termin hinzugefuegt: %s %s", date, title);
    return true;
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
