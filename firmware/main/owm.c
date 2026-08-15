#include "owm.h"
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
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR

static const char *TAG = "owm";
#define CUR_URL "https://api.openweathermap.org/data/2.5/weather?%s&units=metric&lang=de&appid=%s"
#define FC_URL  "https://api.openweathermap.org/data/2.5/forecast?%s&units=metric&lang=de&appid=%s"
#define DEFAULT_LOC "lat=50.008&lon=9.216"   // Johannesberg
#define POLL_INTERVAL_MS   (20 * 60 * 1000)   // aktuelle Werte: 20 min -> 72/Tag
#define FC_EVERY_N_POLLS    9                  // Vorhersage jede 9. Runde -> ~3 h -> 8/Tag

static owm_data_t s_data;
static SemaphoreHandle_t s_lock;
// Serialisiert poll_current()/poll_forecast(): der Hintergrund-Poller und ein
// manueller owm_refresh() duerfen nicht gleichzeitig laufen (poll_current nutzt
// einen static-Puffer, und doppelte Abrufe waeren Verschwendung).
static SemaphoreHandle_t s_poll_gate;

static const char *WD[] = { "Sonntag", "Montag", "Dienstag", "Mittwoch",
                            "Donnerstag", "Freitag", "Samstag" };

static int iround(double v) { return (int)(v >= 0 ? v + 0.5 : v - 0.5); }

static bool get_key(char *key, size_t len)
{
    return config_get_str("owm_key", key, len) && key[0] != '\0';
}

// Standort-Teil der URL: Config "owm_loc" (Ortsname) URL-kodiert als q=..., sonst
// die Johannesberg-Default-Koordinaten.
static void loc_part(char *out, size_t len)
{
    char loc[64];
    if (!config_get_str("owm_loc", loc, sizeof(loc)) || loc[0] == '\0') {
        snprintf(out, len, DEFAULT_LOC);
        return;
    }
    size_t o = 0;
    o += snprintf(out, len, "q=");
    for (size_t i = 0; loc[i] && o + 4 < len; i++) {
        unsigned char c = (unsigned char)loc[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ',' || c == '.' || c == '-') {
            out[o++] = (char)c;
        } else {
            o += snprintf(out + o, len - o, "%%%02X", c);   // URL-kodieren (Leerzeichen/Umlaute)
        }
    }
    out[o] = '\0';
}

// Datum (YYYY-MM-DD) + Wochentag fuer n Tage in der Zukunft.
static void date_offset(int n, char *date, size_t dl, char *wday, size_t wl)
{
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    tm.tm_mday += n; tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    mktime(&tm);   // normalisiert Monat/Jahr und tm_wday
    snprintf(date, dl, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    snprintf(wday, wl, "%s", WD[tm.tm_wday % 7]);
}

// --- aktuelle Werte --------------------------------------------------------
static void poll_current(void)
{
    char key[40];
    if (!get_key(key, sizeof(key))) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_data.has_key = false; s_data.valid = false;
        xSemaphoreGive(s_lock);
        return;
    }

    char loc[96]; loc_part(loc, sizeof(loc));
    char url[320]; snprintf(url, sizeof(url), CUR_URL, loc, key);
    static EXT_RAM_BSS_ATTR char buf[4096];   // Parse-Puffer ins PSRAM (spart internen RAM)
    bool valid = false;
    char desc[48] = { 0 }, icon[4] = { 0 };
    int temp = 0, feels = 0, hum = 0, wind = 0;

    int n = http_get(url, buf, sizeof(buf));
    if (n > 0) {
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            cJSON *main = cJSON_GetObjectItem(root, "main");
            cJSON *w = cJSON_GetObjectItem(root, "wind");
            cJSON *weather = cJSON_GetObjectItem(root, "weather");
            if (cJSON_IsObject(main)) {
                cJSON *t = cJSON_GetObjectItem(main, "temp");
                cJSON *f = cJSON_GetObjectItem(main, "feels_like");
                cJSON *h = cJSON_GetObjectItem(main, "humidity");
                if (cJSON_IsNumber(t)) temp = iround(t->valuedouble);
                if (cJSON_IsNumber(f)) feels = iround(f->valuedouble);
                if (cJSON_IsNumber(h)) hum = h->valueint;
                valid = true;
            }
            if (cJSON_IsArray(weather) && cJSON_GetArraySize(weather) > 0) {
                cJSON *w0 = cJSON_GetArrayItem(weather, 0);
                cJSON *d = cJSON_GetObjectItem(w0, "description");
                cJSON *ic = cJSON_GetObjectItem(w0, "icon");
                if (cJSON_IsString(d)) strncpy(desc, d->valuestring, sizeof(desc) - 1);
                if (cJSON_IsString(ic)) strncpy(icon, ic->valuestring, sizeof(icon) - 1);
            }
            if (cJSON_IsObject(w)) {
                cJSON *sp = cJSON_GetObjectItem(w, "speed");
                if (cJSON_IsNumber(sp)) wind = iround(sp->valuedouble * 3.6);
            }
            cJSON_Delete(root);
        }
    }
    if (!valid) ESP_LOGW(TAG, "Aktuell: Abfrage fehlgeschlagen");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.has_key = true;
    s_data.valid = valid;
    if (valid) {
        strncpy(s_data.desc, desc, sizeof(s_data.desc) - 1);
        strncpy(s_data.icon, icon, sizeof(s_data.icon) - 1);
        s_data.temp = temp; s_data.feels = feels; s_data.humidity = hum; s_data.wind_kmh = wind;
    }
    xSemaphoreGive(s_lock);
}

// --- Vorhersage (morgen / uebermorgen / Tag danach) ------------------------
// Gibt true zurueck, wenn mindestens ein Tag erfolgreich geparst wurde.
static bool poll_forecast(void)
{
    char key[40];
    if (!get_key(key, sizeof(key))) return false;

    char loc[96]; loc_part(loc, sizeof(loc));
    char url[320]; snprintf(url, sizeof(url), FC_URL, loc, key);
    // Die 5-Tage/3-Stunden-Antwort (40 Eintraege, lang=de) liegt bei ~30-36 KB
    // und ist ueber die Zeit ueber die alten 32 KB gewachsen -> http_get schnitt
    // die Antwort ab -> cJSON_Parse scheiterte -> keine Vorhersage. Grosszuegig
    // im PSRAM (wie kino).
    #define FC_BUF (96 * 1024)
    char *buf = heap_caps_malloc(FC_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(FC_BUF);
    if (!buf) return false;

    int n = http_get(url, buf, FC_BUF);
    if (n <= 0) { free(buf); ESP_LOGW(TAG, "Vorhersage: Abruf fehlgeschlagen"); return false; }
    if (n >= FC_BUF - 1) ESP_LOGW(TAG, "Vorhersage: Antwort >= %d B - Puffer evtl. zu klein", FC_BUF);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    cJSON *list = root ? cJSON_GetObjectItem(root, "list") : NULL;
    if (!cJSON_IsArray(list)) { cJSON_Delete(root); return false; }

    // Index 0 = heute, 1..3 = morgen / uebermorgen / Tag danach.
    owm_fc_day_t fc[4];
    char dates[4][11];
    for (int k = 0; k < 4; k++) {
        memset(&fc[k], 0, sizeof(fc[k]));
        char wd[14];
        date_offset(k, dates[k], sizeof(dates[k]), wd, sizeof(wd));
        if (k == 0)      snprintf(fc[k].label, sizeof(fc[k].label), "Heute");
        else if (k == 1) snprintf(fc[k].label, sizeof(fc[k].label), "Morgen");
        else if (k == 2) snprintf(fc[k].label, sizeof(fc[k].label), "\xc3\x9c" "bermorgen");
        else             snprintf(fc[k].label, sizeof(fc[k].label), "%s", wd);
        fc[k].tmin = 999; fc[k].tmax = -999;
    }
    int best_hd[4] = { 99, 99, 99, 99 };   // Abstand zur Mittagszeit fuer die Beschreibung

    cJSON *e;
    cJSON_ArrayForEach(e, list) {
        cJSON *dt_txt = cJSON_GetObjectItem(e, "dt_txt");
        cJSON *main = cJSON_GetObjectItem(e, "main");
        if (!cJSON_IsString(dt_txt) || !cJSON_IsObject(main)) continue;
        const char *dts = dt_txt->valuestring;   // "YYYY-MM-DD HH:MM:SS"
        for (int k = 0; k < 4; k++) {
            if (strncmp(dts, dates[k], 10) != 0) continue;
            cJSON *tmn = cJSON_GetObjectItem(main, "temp_min");
            cJSON *tmx = cJSON_GetObjectItem(main, "temp_max");
            if (cJSON_IsNumber(tmn)) { int v = iround(tmn->valuedouble); if (v < fc[k].tmin) fc[k].tmin = v; }
            if (cJSON_IsNumber(tmx)) { int v = iround(tmx->valuedouble); if (v > fc[k].tmax) fc[k].tmax = v; }
            int hour = (dts[11] - '0') * 10 + (dts[12] - '0');
            int hd = hour > 13 ? hour - 13 : 13 - hour;
            if (hd < best_hd[k]) {
                best_hd[k] = hd;
                cJSON *weather = cJSON_GetObjectItem(e, "weather");
                if (cJSON_IsArray(weather) && cJSON_GetArraySize(weather) > 0) {
                    cJSON *w0 = cJSON_GetArrayItem(weather, 0);
                    cJSON *d = cJSON_GetObjectItem(w0, "description");
                    cJSON *ic = cJSON_GetObjectItem(w0, "icon");
                    if (cJSON_IsString(d)) strncpy(fc[k].desc, d->valuestring, sizeof(fc[k].desc) - 1);
                    if (cJSON_IsString(ic)) strncpy(fc[k].icon, ic->valuestring, sizeof(fc[k].icon) - 1);
                }
            }
            fc[k].valid = true;
            break;
        }
    }
    cJSON_Delete(root);

    bool ok = fc[1].valid || fc[2].valid || fc[3].valid;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.fc_valid = ok;
    memcpy(s_data.fc, &fc[1], sizeof(owm_fc_day_t) * 3);   // morgen..+3 (Slide)
    s_data.today = fc[0];                                  // heute (Zusammenfassung)
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Vorhersage aktualisiert (%d Tage)", fc[1].valid + fc[2].valid + fc[3].valid);
    return ok;
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));
    int  since_fc = 0;      // Runden seit letztem Vorhersage-Abruf
    bool fc_ok = false;     // erste erfolgreiche Vorhersage schon geholt?
    for (;;) {
        xSemaphoreTake(s_poll_gate, portMAX_DELAY);
        poll_current();
        // Vorhersage erst, wenn die Uhr per NTP steht (sonst matchen die
        // Datums-Strings keinen Listeneintrag). Bis zum ersten Erfolg jede
        // Runde (20 min) erneut versuchen, danach nur noch alle ~3 h.
        if (time_sync_is_valid() && (!fc_ok || since_fc >= FC_EVERY_N_POLLS)) {
            fc_ok = poll_forecast();
            since_fc = 0;
        }
        xSemaphoreGive(s_poll_gate);
        since_fc++;
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void owm_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_poll_gate = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "owm", 7168, NULL, 3, NULL);
}

void owm_refresh(void)
{
    if (!s_poll_gate) return;
    xSemaphoreTake(s_poll_gate, portMAX_DELAY);
    poll_current();
    if (time_sync_is_valid()) poll_forecast();
    xSemaphoreGive(s_poll_gate);
}

void owm_get(owm_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
