#include "space.h"
#include "http_util.h"
#include "config_store.h"
#include "time_sync.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "space";

// Kuratierte Liste heller, oft mit blossem Auge sichtbarer Objekte (NORAD).
// N2YO /visualpasses liefert nur nachts sichtbare Ueberfluege + Helligkeit;
// nicht sichtbare/zu dunkle Objekte liefern einfach keine Pässe (unschaedlich).
static const int CURATED[] = {
    25544,   // ISS
    48274,   // Tiangong / CSS
    20580,   // Hubble
    23705,   // SL-16 R/B (helle Zenit-Raketenstufe)
    28353,   // SL-16 R/B
};
#define N_CURATED (int)(sizeof(CURATED) / sizeof(CURATED[0]))

#define N2YO_URL "https://api.n2yo.com/rest/v1/satellite/visualpasses/%d/%s/%s/%s/2/60/&apiKey=%s"
#define NEO_URL  "https://api.nasa.gov/neo/rest/v1/feed?start_date=%s&end_date=%s&api_key=%s"

#define PASS_POLL_MS  (2 * 60 * 60 * 1000)   // Satelliten alle 2 h
#define NEO_POLL_MS   (6 * 60 * 60 * 1000)   // Asteroiden alle 6 h

static sat_passes_t s_passes;
static neos_t       s_neos;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_gate;   // serialisiert die Poll-Funktionen
static TaskHandle_t      s_task;   // fuer space_wake() (Notify)

static void cfg_loc(char *lat, size_t ll, char *lon, size_t nl, char *alt, size_t al)
{
    config_get_str_def("planes_lat", lat, ll, "50.008");
    config_get_str_def("planes_lon", lon, nl, "9.216");
    config_get_str_def("space_alt", alt, al, "200");
    if (!lat[0]) snprintf(lat, ll, "50.008");
    if (!lon[0]) snprintf(lon, nl, "9.216");
    if (!alt[0]) snprintf(alt, al, "200");
}

// Ein Ueberflug in das nach Zeit aufsteigend sortierte Feld einfuegen.
static void pass_insert(sat_pass_t *arr, int *cnt, const sat_pass_t *p)
{
    int pos = *cnt;
    while (pos > 0 && arr[pos - 1].max_utc > p->max_utc) pos--;
    if (pos >= SPACE_MAX_PASSES) return;
    int last = (*cnt < SPACE_MAX_PASSES) ? *cnt : SPACE_MAX_PASSES - 1;
    for (int i = last; i > pos; i--) arr[i] = arr[i - 1];
    arr[pos] = *p;
    if (*cnt < SPACE_MAX_PASSES) (*cnt)++;
}

// --- Satelliten-Ueberfluege (N2YO) -----------------------------------------
static void poll_passes(void)
{
    char key[40];
    bool has_key = config_get_str("n2yo_key", key, sizeof(key)) && key[0];
    if (!has_key) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_passes.has_key = false; s_passes.valid = false; s_passes.count = 0;
        xSemaphoreGive(s_lock);
        return;
    }

    char lat[16], lon[16], alt[8];
    cfg_loc(lat, sizeof(lat), lon, sizeof(lon), alt, sizeof(alt));

    static EXT_RAM_BSS_ATTR char buf[16384];   // N2YO-Antwort (PSRAM)
    sat_pass_t tmp[SPACE_MAX_PASSES];
    int cnt = 0;
    bool any_ok = false;

    for (int i = 0; i < N_CURATED; i++) {
        char url[256];
        snprintf(url, sizeof(url), N2YO_URL, CURATED[i], lat, lon, alt, key);
        int n = http_get(url, buf, sizeof(buf));
        if (n <= 0) continue;
        cJSON *root = cJSON_Parse(buf);
        if (!root) continue;
        any_ok = true;

        cJSON *info = cJSON_GetObjectItem(root, "info");
        cJSON *nm = info ? cJSON_GetObjectItem(info, "satname") : NULL;
        char name[24] = "?";
        if (cJSON_IsString(nm)) snprintf(name, sizeof(name), "%s", nm->valuestring);

        cJSON *passes = cJSON_GetObjectItem(root, "passes");
        if (cJSON_IsArray(passes)) {
            cJSON *e;
            cJSON_ArrayForEach(e, passes) {
                sat_pass_t p; memset(&p, 0, sizeof(p));
                snprintf(p.name, sizeof(p.name), "%s", name);
                cJSON *mu = cJSON_GetObjectItem(e, "maxUTC");
                cJSON *me = cJSON_GetObjectItem(e, "maxEl");
                cJSON *dc = cJSON_GetObjectItem(e, "maxAzCompass");
                cJSON *mg = cJSON_GetObjectItem(e, "mag");
                if (cJSON_IsNumber(mu)) p.max_utc = (time_t)mu->valuedouble;
                if (cJSON_IsNumber(me)) p.max_el = (int)(me->valuedouble + 0.5);
                if (cJSON_IsString(dc)) snprintf(p.dir, sizeof(p.dir), "%s", dc->valuestring);
                p.mag = cJSON_IsNumber(mg) ? mg->valuedouble : 99.0;
                pass_insert(tmp, &cnt, &p);
            }
        }
        cJSON_Delete(root);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_passes.has_key = true;
    s_passes.valid = any_ok;
    s_passes.count = cnt;
    memcpy(s_passes.p, tmp, sizeof(sat_pass_t) * cnt);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d sichtbare Ueberfluege", cnt);
}

// --- Asteroiden / erdnahe Objekte (NASA NeoWs) -----------------------------
static void neo_insert(neo_t *arr, int *cnt, const neo_t *o)
{
    int pos = *cnt;
    while (pos > 0 && arr[pos - 1].miss_km > o->miss_km) pos--;
    if (pos >= SPACE_MAX_NEOS) return;
    int last = (*cnt < SPACE_MAX_NEOS) ? *cnt : SPACE_MAX_NEOS - 1;
    for (int i = last; i > pos; i--) arr[i] = arr[i - 1];
    arr[pos] = *o;
    if (*cnt < SPACE_MAX_NEOS) (*cnt)++;
}

static void poll_neos(void)
{
    // NeoWs laeuft mit dem oeffentlichen DEMO_KEY (Limit ~50/Tag; wir fragen
    // nur alle 6 h) - solange kein eigener Key gesetzt ist.
    char key[48];
    config_get_str_def("nasa_key", key, sizeof(key), "DEMO_KEY");
    if (!key[0]) snprintf(key, sizeof(key), "DEMO_KEY");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_neos.has_key = true;   // via DEMO_KEY immer nutzbar
    xSemaphoreGive(s_lock);
    if (!time_sync_is_valid()) return;   // Datum noch nicht bekannt

    char today[16];
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    strftime(today, sizeof(today), "%Y-%m-%d", &tm);

    char url[256];
    snprintf(url, sizeof(url), NEO_URL, today, today, key);

    static EXT_RAM_BSS_ATTR char buf[98304];   // 96 KB (Tages-Feed kann gross sein)
    int n = http_get(url, buf, sizeof(buf));
    if (n <= 0) { ESP_LOGW(TAG, "NeoWs-Abruf fehlgeschlagen"); return; }

    cJSON *root = cJSON_Parse(buf);
    if (!root) { ESP_LOGW(TAG, "NeoWs-Parse fehlgeschlagen"); return; }

    neo_t tmp[SPACE_MAX_NEOS];
    int cnt = 0;
    cJSON *neo_by_date = cJSON_GetObjectItem(root, "near_earth_objects");
    cJSON *arr = neo_by_date ? cJSON_GetObjectItem(neo_by_date, today) : NULL;
    if (cJSON_IsArray(arr)) {
        cJSON *e;
        cJSON_ArrayForEach(e, arr) {
            neo_t o; memset(&o, 0, sizeof(o));
            cJSON *nm = cJSON_GetObjectItem(e, "name");
            if (cJSON_IsString(nm)) snprintf(o.name, sizeof(o.name), "%s", nm->valuestring);
            cJSON *hz = cJSON_GetObjectItem(e, "is_potentially_hazardous_asteroid");
            o.hazard = cJSON_IsTrue(hz);

            cJSON *ed = cJSON_GetObjectItem(e, "estimated_diameter");
            cJSON *edm = ed ? cJSON_GetObjectItem(ed, "meters") : NULL;
            if (edm) {
                cJSON *dmin = cJSON_GetObjectItem(edm, "estimated_diameter_min");
                cJSON *dmax = cJSON_GetObjectItem(edm, "estimated_diameter_max");
                double a = cJSON_IsNumber(dmin) ? dmin->valuedouble : 0;
                double b = cJSON_IsNumber(dmax) ? dmax->valuedouble : 0;
                o.diam_m = (a + b) / 2.0;
            }

            cJSON *cad = cJSON_GetObjectItem(e, "close_approach_data");
            if (cJSON_IsArray(cad) && cJSON_GetArraySize(cad) > 0) {
                cJSON *c0 = cJSON_GetArrayItem(cad, 0);
                cJSON *md = cJSON_GetObjectItem(c0, "miss_distance");
                cJSON *mdk = md ? cJSON_GetObjectItem(md, "kilometers") : NULL;
                if (cJSON_IsString(mdk)) o.miss_km = atof(mdk->valuestring);
                cJSON *ep = cJSON_GetObjectItem(c0, "epoch_date_close_approach");
                if (cJSON_IsNumber(ep)) o.approach_utc = (time_t)(ep->valuedouble / 1000.0);
            }
            if (o.miss_km <= 0) o.miss_km = 1e12;   // ohne Abstand ans Ende
            neo_insert(tmp, &cnt, &o);
        }
    }
    cJSON_Delete(root);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_neos.has_key = true;
    s_neos.valid = true;
    s_neos.count = cnt;
    memcpy(s_neos.n, tmp, sizeof(neo_t) * cnt);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d erdnahe Objekte heute", cnt);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(12000));   // Netz/NTP Zeit zum Hochlaufen geben
    int64_t since_neo = NEO_POLL_MS;    // beim ersten Durchlauf gleich holen
    for (;;) {
        xSemaphoreTake(s_gate, portMAX_DELAY);
        poll_passes();
        if (since_neo >= NEO_POLL_MS) { poll_neos(); since_neo = 0; }
        xSemaphoreGive(s_gate);
        since_neo += PASS_POLL_MS;
        // Bis zum naechsten Turnus warten ODER per Notify (Key-Aenderung) sofort
        // aufwachen; nach einem Notify auch die Asteroiden gleich neu holen.
        uint32_t woke = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PASS_POLL_MS));
        if (woke) since_neo = NEO_POLL_MS;
    }
}

void space_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_gate = xSemaphoreCreateMutex();
    memset(&s_passes, 0, sizeof(s_passes));
    memset(&s_neos, 0, sizeof(s_neos));
    xTaskCreate(poll_task, "space", 7168, NULL, 3, &s_task);
}

void space_wake(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

void space_get_passes(sat_passes_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_passes;
    xSemaphoreGive(s_lock);
}

void space_get_neos(neos_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_neos;
    xSemaphoreGive(s_lock);
}
