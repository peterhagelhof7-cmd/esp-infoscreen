#include "planes.h"
#include "http_util.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR

static const char *TAG = "planes";

// adsb.lol: Punkt-Abfrage (kostenlos, ohne API-Key, gleiches v2-Format wie
// airplanes.live). Radius in nautischen Meilen (max. 250). airplanes.live
// selbst hat den offenen Zugang zugunsten einer Registrierungspflicht
// eingestellt (HTTP 403 mit Kontakt-Aufforderung), daher adsb.lol.
#define URL_FMT  "https://api.adsb.lol/v2/point/%s/%s/%s"
#define DEF_LAT  "50.008"    // Johannesberg
#define DEF_LON  "9.216"
#define DEF_RAD  "20"        // nautische Meilen

#define POLL_INTERVAL_MS  (60 * 1000)   // 1x/min (Rate-Limit ~1 Anfrage/s, hoefliche Nutzung)

static planes_data_t s_data;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_poll_gate;   // serialisiert Poller und manuellen Refresh

// Standort-/Radius-Konfig lesen (mit Johannesberg-Defaults).
static void cfg_loc(char *lat, size_t ll, char *lon, size_t nl, char *rad, size_t rl)
{
    config_get_str_def("planes_lat", lat, ll, DEF_LAT);
    config_get_str_def("planes_lon", lon, nl, DEF_LON);
    config_get_str_def("planes_radius", rad, rl, DEF_RAD);
    if (!lat[0]) snprintf(lat, ll, DEF_LAT);
    if (!lon[0]) snprintf(lon, nl, DEF_LON);
    if (!rad[0]) snprintf(rad, rl, DEF_RAD);
}

// Grosskreis-Entfernung (nautische Meilen) zwischen Standort und Flugzeug.
static double haversine_nm(double lat1, double lon1, double lat2, double lon2)
{
    const double R_NM = 3440.065;   // Erdradius in nautischen Meilen
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dlon / 2) * sin(dlon / 2);
    return R_NM * 2 * atan2(sqrt(a), sqrt(1 - a));
}

// Ein Flugzeug in das nach Entfernung aufsteigend sortierte Feld einfuegen
// (fixe Kapazitaet PLANES_MAX; weiter entfernte fallen hinten heraus).
static void insert_sorted(plane_t *arr, int *cnt, const plane_t *p)
{
    int pos = *cnt;
    while (pos > 0 && arr[pos - 1].dst_nm > p->dst_nm) pos--;
    if (pos >= PLANES_MAX) return;                     // weiter weg als alle -> verwerfen
    int last = (*cnt < PLANES_MAX) ? *cnt : PLANES_MAX - 1;
    for (int i = last; i > pos; i--) arr[i] = arr[i - 1];
    arr[pos] = *p;
    if (*cnt < PLANES_MAX) (*cnt)++;
}

// --- Routen-Cache (Callsign -> Start/Ziel via adsbdb.com) -------------------
// ADS-B liefert keine Route. adsbdb.com gibt pro Callsign Start-/Zielflughafen
// mit Stadt (municipality). Callsign->Route ist tagesstabil -> zwischenspeichern,
// damit nicht jeder Poll erneut abfragt. Zugriff nur aus poll_once() (s_poll_gate).
#define ROUTE_URL   "https://api.adsbdb.com/v0/callsign/%s"
#define ROUTE_CACHE 24

typedef struct {
    char cs[10];        // Callsign
    char from[28];      // Start-Stadt (leer = keine Route bekannt)
    char to[28];        // Ziel-Stadt
    bool done;          // Lookup schon versucht?
} route_cache_t;
static route_cache_t s_routes[ROUTE_CACHE];
static int s_route_next;   // Ring-Einfuegeposition

// Ortsnamen aus einem adsbdb origin/destination-Objekt: bevorzugt die Stadt
// (municipality), sonst der IATA-Code.
static void place_name(cJSON *obj, char *out, size_t len)
{
    if (!cJSON_IsObject(obj)) return;
    cJSON *muni = cJSON_GetObjectItem(obj, "municipality");
    cJSON *iata = cJSON_GetObjectItem(obj, "iata_code");
    if (cJSON_IsString(muni) && muni->valuestring[0])
        snprintf(out, len, "%s", muni->valuestring);
    else if (cJSON_IsString(iata) && iata->valuestring[0])
        snprintf(out, len, "%s", iata->valuestring);
}

static int route_find(const char *cs)
{
    for (int i = 0; i < ROUTE_CACHE; i++)
        if (s_routes[i].done && strcmp(s_routes[i].cs, cs) == 0) return i;
    return -1;
}

// Route fuer ein Callsign abrufen und im Cache ablegen (auch bei "keine Route"
// -> done=true, damit dasselbe Callsign nicht bei jedem Poll neu abgefragt wird).
static void route_fetch(const char *cs)
{
    char url[96]; snprintf(url, sizeof(url), ROUTE_URL, cs);
    static EXT_RAM_BSS_ATTR char rbuf[4096];
    int n = http_get(url, rbuf, sizeof(rbuf));

    route_cache_t *slot = &s_routes[s_route_next % ROUTE_CACHE];
    s_route_next++;
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->cs, sizeof(slot->cs), "%s", cs);
    slot->done = true;

    if (n <= 0) return;
    cJSON *root = cJSON_Parse(rbuf);
    if (!root) return;
    cJSON *resp = cJSON_GetObjectItem(root, "response");   // sonst String "unknown callsign"
    if (cJSON_IsObject(resp)) {
        cJSON *fr = cJSON_GetObjectItem(resp, "flightroute");
        if (cJSON_IsObject(fr)) {
            place_name(cJSON_GetObjectItem(fr, "origin"),      slot->from, sizeof(slot->from));
            place_name(cJSON_GetObjectItem(fr, "destination"), slot->to,   sizeof(slot->to));
        }
    }
    cJSON_Delete(root);
}

// Route der Flugzeuge auffuellen (aus Cache; bis route_cap neue Lookups/Aufruf,
// um TLS-Bursts zu begrenzen). Bereits gecachte kosten nichts.
static void enrich_routes(plane_t *arr, int cnt, int route_cap)
{
    int fetched = 0;
    for (int i = 0; i < cnt; i++) {
        if (!arr[i].flight[0]) continue;
        int ri = route_find(arr[i].flight);
        if (ri < 0 && fetched < route_cap) {
            route_fetch(arr[i].flight);
            fetched++;
            ri = route_find(arr[i].flight);
        }
        if (ri >= 0) {
            snprintf(arr[i].from, sizeof(arr[i].from), "%s", s_routes[ri].from);
            snprintf(arr[i].to,   sizeof(arr[i].to),   "%s", s_routes[ri].to);
        }
    }
}

static void poll_once(int route_cap)
{
    char lat[16], lon[16], rad[8];
    cfg_loc(lat, sizeof(lat), lon, sizeof(lon), rad, sizeof(rad));
    double olat = atof(lat), olon = atof(lon);

    char url[128];
    snprintf(url, sizeof(url), URL_FMT, lat, lon, rad);

    // Antwort kann bei vielen Flugzeugen gross werden (Umkreis Frankfurt) -> ein
    // grosszuegiger Parse-Puffer im PSRAM; wird http_get truncatet, scheitert
    // cJSON sonst komplett.
    static EXT_RAM_BSS_ATTR char buf[262144];   // 256 KB

    int n = http_get(url, buf, sizeof(buf));
    if (n <= 0) {
        // adsb.lol schliesst die Verbindung gelegentlich ("connection reset")
        // -> ein Retry. Bleibt es dabei, die LETZTE gute Liste behalten (kein
        // valid=false), sonst flackert Slide/Bot bei kurzen Netz-Haengern.
        vTaskDelay(pdMS_TO_TICKS(1500));
        n = http_get(url, buf, sizeof(buf));
    }
    if (n <= 0) {
        ESP_LOGW(TAG, "Abruf fehlgeschlagen (%d) - letzte Liste bleibt", n);
        return;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON-Parse fehlgeschlagen - letzte Liste bleibt");
        return;
    }

    // Das "ac"-Array haelt die Flugzeuge (bei manchen Antworten "aircraft").
    cJSON *ac = cJSON_GetObjectItem(root, "ac");
    if (!cJSON_IsArray(ac)) ac = cJSON_GetObjectItem(root, "aircraft");

    plane_t tmp[PLANES_MAX];
    int cnt = 0;

    if (cJSON_IsArray(ac)) {
        int total = cJSON_GetArraySize(ac);
        for (int i = 0; i < total; i++) {
            cJSON *a = cJSON_GetArrayItem(ac, i);
            if (!cJSON_IsObject(a)) continue;

            plane_t p; memset(&p, 0, sizeof(p));
            p.track = -1;

            cJSON *fl = cJSON_GetObjectItem(a, "flight");
            if (cJSON_IsString(fl)) {
                snprintf(p.flight, sizeof(p.flight), "%s", fl->valuestring);
                // fuehrende/anhaengende Leerzeichen trimmen (API paddet oft)
                for (int k = (int)strlen(p.flight) - 1; k >= 0 && p.flight[k] == ' '; k--) p.flight[k] = '\0';
            }
            cJSON *ty = cJSON_GetObjectItem(a, "t");
            if (cJSON_IsString(ty)) snprintf(p.type, sizeof(p.type), "%s", ty->valuestring);
            cJSON *rg = cJSON_GetObjectItem(a, "r");
            if (cJSON_IsString(rg)) snprintf(p.reg, sizeof(p.reg), "%s", rg->valuestring);

            cJSON *alt = cJSON_GetObjectItem(a, "alt_baro");
            if (cJSON_IsNumber(alt))      p.alt_ft = alt->valueint;
            else if (cJSON_IsString(alt)) p.alt_ft = -1;   // "ground"
            else                          p.alt_ft = 0;

            cJSON *gs = cJSON_GetObjectItem(a, "gs");
            if (cJSON_IsNumber(gs)) p.gs_kt = (int)(gs->valuedouble + 0.5);
            cJSON *tr = cJSON_GetObjectItem(a, "track");
            if (cJSON_IsNumber(tr)) p.track = (int)(tr->valuedouble + 0.5);

            // Entfernung: API-Feld "dst" bevorzugen, sonst aus lat/lon berechnen.
            cJSON *dst = cJSON_GetObjectItem(a, "dst");
            if (cJSON_IsNumber(dst)) {
                p.dst_nm = dst->valuedouble;
            } else {
                cJSON *plat = cJSON_GetObjectItem(a, "lat");
                cJSON *plon = cJSON_GetObjectItem(a, "lon");
                if (cJSON_IsNumber(plat) && cJSON_IsNumber(plon))
                    p.dst_nm = haversine_nm(olat, olon, plat->valuedouble, plon->valuedouble);
                else
                    p.dst_nm = 9999.0;   // ohne Position ans Ende
            }

            insert_sorted(tmp, &cnt, &p);
        }
    }

    cJSON_Delete(root);

    enrich_routes(tmp, cnt, route_cap);   // Start/Ziel nachschlagen (gecacht)

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.valid = true;
    s_data.count = cnt;
    memcpy(s_data.ac, tmp, sizeof(plane_t) * cnt);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d Flugzeug(e) in der Naehe", cnt);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(8000));   // etwas nach dem Boot starten
    for (;;) {
        xSemaphoreTake(s_poll_gate, portMAX_DELAY);
        poll_once(3);   // Hintergrund: max. 3 neue Routen-Lookups/Poll (gg. TLS-Bursts)
        xSemaphoreGive(s_poll_gate);
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void planes_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_poll_gate = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "planes", 6144, NULL, 3, NULL);
}

void planes_refresh(void)
{
    if (!s_poll_gate) return;
    xSemaphoreTake(s_poll_gate, portMAX_DELAY);
    poll_once(PLANES_MAX);   // Bot-Abruf: alle gezeigten Flugzeuge mit Route
    xSemaphoreGive(s_poll_gate);
}

void planes_get(planes_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
