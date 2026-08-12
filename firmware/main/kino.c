#include "kino.h"
#include "http_util.h"
#include "time_sync.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "kino";

// kino.de-Backend (keyless). Cinema-ID per Config "kino_id" (Default KINOPOLIS
// Aschaffenburg = 1405; Casino Aschaffenburg waere 1711).
#define KINO_DEFAULT_ID "1405"
#define MAX_MOVIES 40
// Antwort waechst mit der Zahl der Vorstellungen (Aug 2026 bereits ~67 KB:
// 12 Filme / 250 Showtimes). Grosszuegig dimensioniert, damit http_get die
// Antwort nicht abschneidet -> sonst scheitert cJSON_Parse an unvollstaendigem
// JSON und der Slide bleibt leer ("noch nicht geladen"). Puffer liegt im PSRAM.
#define PARSE_BUF  (192 * 1024)

typedef struct {
    char title[80];
    int  fsk;             // -1 = unbekannt
    char min_date[11];    // fruehestes lokales Vorstellungsdatum YYYY-MM-DD
    char max_date[11];    // spaetestes
} kino_movie_t;

static EXT_RAM_BSS_ATTR kino_movie_t s_movies[MAX_MOVIES];   // PSRAM (Cache)
static int   s_count;
static char  s_cinema[64];
static bool  s_valid;
static char  s_last_fetch_day[11];     // lokaler Tag des letzten Erfolgs
static SemaphoreHandle_t s_lock;

// Temporaerpuffer fuer den Aufbau (nur im Telegram-Task benutzt -> static/PSRAM
// haelt den knappen Task-Stack frei; nicht reentrant, aber der Abruf laeuft nur
// aus genau einem Task).
static EXT_RAM_BSS_ATTR kino_movie_t tmp_movies[MAX_MOVIES];
static EXT_RAM_BSS_ATTR char         tmp_ids[MAX_MOVIES][16];

// --- ISO-8601-UTC ("...THH:MM:SSZ") -> lokales Datum "YYYY-MM-DD" ------------
static void showtime_local_date(const char *iso, char *out /* >=11 */)
{
    out[0] = '\0';
    int Y, M, D, h, mi;
    if (sscanf(iso, "%d-%d-%dT%d:%d", &Y, &M, &D, &h, &mi) != 5) return;
    // Tage seit 1970 aus UTC-Datum (Howard Hinnants days_from_civil).
    int y = Y - (M <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(M + (M > 2 ? -3 : 9)) + 2) / 5 + (unsigned)D - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    time_t ep = (time_t)days * 86400 + h * 3600 + mi * 60;
    struct tm lt;
    localtime_r(&ep, &lt);   // wandelt in lokale Zeit (Geraete-TZ CET/CEST)
    strftime(out, 11, "%Y-%m-%d", &lt);
}

// Datum des naechsten Montags (lokal) -> iso "YYYY-MM-DD" und/oder "DD.MM.".
static void next_monday(char *iso /* >=11 or NULL */, char *human /* >=8 or NULL */)
{
    time_t now = time(NULL);
    struct tm lt; localtime_r(&now, &lt);
    lt.tm_hour = 12; lt.tm_min = 0; lt.tm_sec = 0;   // Mittag -> DST-robust
    int offset = (lt.tm_wday + 6) % 7;               // Tage seit Montag dieser Woche
    time_t base = mktime(&lt);
    time_t nm = base + (time_t)(7 - offset) * 86400; // Montag der naechsten Woche
    struct tm t2; localtime_r(&nm, &t2);
    if (iso)   strftime(iso, 11, "%Y-%m-%d", &t2);
    if (human) strftime(human, 8, "%d.%m.", &t2);
}

// --- Abruf + Parsen ---------------------------------------------------------
static bool fetch_and_parse(void)
{
    char id[16];
    config_get_str_def("kino_id", id, sizeof(id), KINO_DEFAULT_ID);
    char url[96];
    snprintf(url, sizeof(url), "https://kntkapi.apps.stroeermb.de/api/cinema/%s/", id);

    char *buf = heap_caps_malloc(PARSE_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(PARSE_BUF);
    if (!buf) return false;

    int n = http_get(url, buf, PARSE_BUF);
    if (n <= 0) { free(buf); ESP_LOGW(TAG, "Abruf fehlgeschlagen"); return false; }
    if (n >= (int)PARSE_BUF - 1) {   // Puffer randvoll -> Antwort evtl. abgeschnitten
        ESP_LOGW(TAG, "Antwort >= %d B - PARSE_BUF vergroessern (JSON evtl. unvollstaendig)", (int)PARSE_BUF);
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "JSON-Parse fehlgeschlagen"); return false; }

    char cinema[64] = { 0 };
    cJSON *jn = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(jn)) snprintf(cinema, sizeof(cinema), "%s", jn->valuestring);

    // Filme einsammeln (id -> Titel/FSK)
    int count = 0;
    cJSON *movies = cJSON_GetObjectItem(root, "movies");
    cJSON *m;
    cJSON_ArrayForEach(m, movies) {
        if (count >= MAX_MOVIES) break;
        cJSON *jid = cJSON_GetObjectItem(m, "id");
        cJSON *jt  = cJSON_GetObjectItem(m, "title");
        if (!cJSON_IsString(jt)) continue;
        char idbuf[16];
        const char *mid = NULL;
        if (cJSON_IsString(jid)) {
            mid = jid->valuestring;
        } else if (cJSON_IsNumber(jid)) {
            snprintf(idbuf, sizeof(idbuf), "%.0f", jid->valuedouble);
            mid = idbuf;
        }
        if (!mid) continue;   // ohne id kein Showtime-Zuordnung moeglich
        snprintf(tmp_ids[count], sizeof(tmp_ids[0]), "%s", mid);
        snprintf(tmp_movies[count].title, sizeof(tmp_movies[0].title), "%s", jt->valuestring);
        cJSON *jf = cJSON_GetObjectItem(m, "fsk");
        tmp_movies[count].fsk = cJSON_IsNumber(jf) ? jf->valueint : -1;
        tmp_movies[count].min_date[0] = '\0';
        tmp_movies[count].max_date[0] = '\0';
        count++;
    }

    // Showtimes -> min/max lokales Datum je Film
    cJSON *shows = cJSON_GetObjectItem(root, "showtimes");
    cJSON *sh;
    cJSON_ArrayForEach(sh, shows) {
        cJSON *smv = cJSON_GetObjectItem(sh, "movie");
        cJSON *sid = smv ? cJSON_GetObjectItem(smv, "id") : NULL;
        cJSON *st  = cJSON_GetObjectItem(sh, "showtime");
        if (!cJSON_IsString(st)) continue;
        const char *mid = cJSON_IsString(sid) ? sid->valuestring : NULL;
        if (!mid) continue;
        int idx = -1;
        for (int i = 0; i < count; i++) if (strcmp(tmp_ids[i], mid) == 0) { idx = i; break; }
        if (idx < 0) continue;
        char d[11];
        showtime_local_date(st->valuestring, d);
        if (!d[0]) continue;
        if (!tmp_movies[idx].min_date[0] || strcmp(d, tmp_movies[idx].min_date) < 0)
            strcpy(tmp_movies[idx].min_date, d);
        if (!tmp_movies[idx].max_date[0] || strcmp(d, tmp_movies[idx].max_date) > 0)
            strcpy(tmp_movies[idx].max_date, d);
    }
    cJSON_Delete(root);

    // Filme ohne jede Vorstellung verwerfen
    int kept = 0;
    for (int i = 0; i < count; i++)
        if (tmp_movies[i].max_date[0]) tmp_movies[kept++] = tmp_movies[i];

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_movies, tmp_movies, sizeof(kino_movie_t) * kept);
    s_count = kept;
    snprintf(s_cinema, sizeof(s_cinema), "%s", cinema[0] ? cinema : "Kino");
    s_valid = true;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d Filme geladen (%s)", kept, s_cinema);
    return true;
}

void kino_refresh_if_due(void)
{
    if (!s_lock) return;
    char today[11];
    time_today_str(today, sizeof(today));
    if (!today[0]) return;                    // Uhr noch nicht gestellt
    if (s_valid && strcmp(today, s_last_fetch_day) == 0) return;   // heute schon geladen
    if (fetch_and_parse()) snprintf(s_last_fetch_day, sizeof(s_last_fetch_day), "%s", today);
}

// --- Nachrichtenaufbau ------------------------------------------------------
// Filme mit max_date >= from_date auflisten (nur Titel + FSK). Ausgabe wird bei
// ~600 Rohzeichen gekappt (haelt die Telegram-GET-URL sicher im Puffer).
static void build(char *out, size_t len, const char *header, const char *from_date)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_valid) {
        snprintf(out, len, "\xF0\x9F\x8E\xAC Kinoprogramm noch nicht geladen.");
        xSemaphoreGive(s_lock);
        return;
    }
    size_t o = snprintf(out, len, "%s", header);
    int shown = 0;
    for (int i = 0; i < s_count && o < len - 1; i++) {
        if (from_date[0] && strcmp(s_movies[i].max_date, from_date) < 0) continue;  // laeuft nicht mehr im Fenster
        if (o > 600) { o += snprintf(out + o, len - o, "\n\xE2\x80\xA6"); break; }  // Kappung "…"
        if (s_movies[i].fsk >= 0)
            o += snprintf(out + o, len - o, "\n\xE2\x80\xA2 %s (FSK%d)", s_movies[i].title, s_movies[i].fsk);
        else
            o += snprintf(out + o, len - o, "\n\xE2\x80\xA2 %s", s_movies[i].title);
        shown++;
    }
    if (shown == 0) snprintf(out + o, len - o, "\n(keine Vorstellungen im Zeitraum)");
    xSemaphoreGive(s_lock);
}

void kino_build_current(char *out, size_t len)
{
    char today[11];
    time_today_str(today, sizeof(today));
    char header[96];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(header, sizeof(header), "\xF0\x9F\x8E\xAC %s \xE2\x80\x94 Programm:",
             s_valid && s_cinema[0] ? s_cinema : "Kino");
    xSemaphoreGive(s_lock);
    build(out, len, header, today);   // alles ab heute
}

void kino_build_preview(char *out, size_t len)
{
    char nm_iso[11], nm_human[8];
    next_monday(nm_iso, nm_human);
    char header[96];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(header, sizeof(header), "\xF0\x9F\x8E\xAC %s \xE2\x80\x94 ab Mo %s:",
             s_valid && s_cinema[0] ? s_cinema : "Kino", nm_human);
    xSemaphoreGive(s_lock);
    build(out, len, header, nm_iso);   // Filme ab naechstem Montag
}

void kino_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_valid = false;
    s_count = 0;
    s_last_fetch_day[0] = '\0';
}
