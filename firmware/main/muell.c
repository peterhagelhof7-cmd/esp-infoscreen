#include "muell.h"
#include "http_util.h"
#include "time_sync.h"
#include "config_store.h"
#include "hc_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "muell";
// Zwei alternative Quellen, Auswahl per Config "muell_src" (nur beim Start gelesen):
//  - MyMuell/jumomind: Ortsteil-ID (city_id=area_id) per Config "muell_id",
//    Default Johannesberg-Oberafferbach = 44886. Wird vom Landkreis
//    Aschaffenburg nur noch bis 31.12.2026 bedient.
//  - AWIDO (cubefour, "LK Aschaffenburg Abfall-App"): Mandant "muell_aw_cl"
//    (z.B. lra-ab) + Orts-/Strassen-/Hausnummer-ID "muell_aw_id"; Termine als
//    ICS je Jahr (KalenderICS.aspx), zeilenweise geparst (kein cJSON noetig).
#define MAX_ENTRIES 48                           // ~ein Jahr Abfuhrtermine vorhalten
#define PARSE_BUF   32768                        // MyMuell-JSON
#define ICS_BUF     (128 * 1024)                 // AWIDO-ICS je Jahr (~45 KB fuer Oberafferbach)
#define ICS_MAX     320                          // Rohtermine aus bis zu 2 Jahren (vor Filter/Sortierung)
#define POLL_INTERVAL_MS (6 * 60 * 60 * 1000)   // alle 6 Stunden
#define RETRY_MS         (15 * 60 * 1000)       // nach Fehlschlag / ohne Uhrzeit frueher erneut

#define AWIDO_BASE "https://awido.cubefour.de"

static EXT_RAM_BSS_ATTR muell_entry_t s_list[MAX_ENTRIES];   // PSRAM
static int s_count;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static muell_src_t s_src;
static time_t s_last_ok;

// --- AWIDO-Mandantenkatalog ---------------------------------------------------
// Es gibt keine oeffentliche Liste aller Mandanten; Stand aus dem Home-Assistant-
// Projekt "waste_collection_schedule" (awido_de.py), am 09.10.2026 alle live
// geprueft (getPlaces -> 200). Fehlende Mandanten per Freitext in der Web-UI.
static const struct { const char *id, *title; } AWIDO_CATALOG[] = {
    { "awld",              "Abfallwirtschaft Lahn-Dill-Kreis" },
    { "awb-altenburg",     "Abfallwirtschaft Altenburger Land" },
    { "awv-isar-inn",      "Abfallwirtschaft Isar-Inn" },
    { "rmk",               "Abfallwirtschaft Rems-Murr (AWRM)" },
    { "awv-nordschwaben",  "Abfall-Wirtschafts-Verband Nordschwaben" },
    { "azv-hef-rof",       "AZV Hersfeld-Rotenburg" },
    { "awb-ak",            "AWB Landkreis Altenkirchen" },
    { "awb-duerkheim",     "AWB Landkreis Bad Dürkheim" },
    { "ffb",               "AWB Landkreis Fürstenfeldbruck" },
    { "ebu",               "EBU Ulm" },
    { "unterhaching",      "Gemeinde Unterhaching" },
    { "ansbach",           "Landkreis Ansbach" },
    { "lra-ab",            "Landkreis Aschaffenburg" },
    { "bgl",               "Landkreis Berchtesgadener Land" },
    { "coburg",            "Landkreis Coburg" },
    { "ebe",               "Landkreis Ebersberg" },
    { "erding",            "Landkreis Erding" },
    { "fulda",             "Landkreis Fulda" },
    { "lkgi",              "Landkreis Gießen" },
    { "gifhorn",           "Landkreis Gifhorn" },
    { "gotha",             "Landkreis Gotha" },
    { "kaw-guenzburg",     "Landkreis Günzburg" },
    { "kelheim",           "Landkreis Kelheim" },
    { "kronach",           "Landkreis Kronach" },
    { "kulmbach",          "Landkreis Kulmbach" },
    { "lichtenfels",       "Landkreis Lichtenfels" },
    { "lra-mue",           "Landkreis Mühldorf a. Inn" },
    { "rosenheim",         "Landkreis Rosenheim" },
    { "roth",              "Landkreis Roth" },
    { "lra-schweinfurt",   "Landkreis Schweinfurt" },
    { "eww-suew",          "Landkreis Südliche Weinstraße" },
    { "kreis-tir",         "Landkreis Tirschenreuth" },
    { "tuebingen",         "Landkreis Tübingen" },
    { "landkreisbetriebe", "Landkreisbetriebe Neuburg-Schrobenhausen" },
    { "aic-fdb",           "Landratsamt Aichach-Friedberg" },
    { "lra-dah",           "Landratsamt Dachau" },
    { "lra-regensburg",    "Landratsamt Regensburg" },
    { "neustadt",          "Neustadt a.d. Waldnaab" },
    { "pullach",           "Pullach im Isartal" },
    { "fulda-stadt",       "Stadt Fulda" },
    { "kaufbeuren",        "Stadt Kaufbeuren" },
    { "koenigstein",       "Stadt Königstein im Taunus" },
    { "memmingen",         "Stadt Memmingen" },
    { "regensburg",        "Stadt Regensburg" },
    { "unterschleissheim", "Stadt Unterschleißheim" },
    { "wgv",               "WGV Recycling GmbH" },
    { "zaso",              "Zweckverband Abfallwirtschaft Saale-Orla" },
    { "zv-muc-so",         "Zweckverband München-Südost" },
};
#define N_CATALOG ((int)(sizeof(AWIDO_CATALOG) / sizeof(AWIDO_CATALOG[0])))

int muell_awido_catalog_count(void) { return N_CATALOG; }
const char *muell_awido_catalog_id(int i)    { return (i >= 0 && i < N_CATALOG) ? AWIDO_CATALOG[i].id : ""; }
const char *muell_awido_catalog_title(int i) { return (i >= 0 && i < N_CATALOG) ? AWIDO_CATALOG[i].title : ""; }

// --- Helfer ----------------------------------------------------------------
// UTF-8-sicher kopieren: kein abgeschnittenes Mehrbyte-Zeichen am Ende.
static void copy_utf8(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t n = src_len < dst_len - 1 ? src_len : dst_len - 1;
    if (n < src_len) {   // gekuerzt: auf Zeichenanfang zurueckgehen
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool valid_client(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 32) return false;
    for (size_t i = 0; i < n; i++)
        if (!(islower((unsigned char)s[i]) || isdigit((unsigned char)s[i]) || s[i] == '-')) return false;
    return true;
}

static bool valid_guid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 40) return false;
    for (size_t i = 0; i < n; i++)
        if (!(isxdigit((unsigned char)s[i]) || s[i] == '-')) return false;
    return true;
}

bool muell_awido_valid(const char *client, const char *id)
{
    return client && id && valid_client(client) && valid_guid(id);
}

static void publish(const muell_entry_t *tmp, int count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_list, tmp, count * sizeof(muell_entry_t));
    s_count = count;
    s_last_ok = time(NULL);
    xSemaphoreGive(s_lock);
}

// --- Quelle MyMuell/jumomind (unveraendert) ----------------------------------
static bool poll_mymuell(const char *today)
{
    char *buf = heap_caps_malloc(PARSE_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(PARSE_BUF);
    if (!buf) return false;

    char id[16]; config_get_str_def("muell_id", id, sizeof(id), MUELL_DEF_ID);
    char url[192];
    snprintf(url, sizeof(url),
             "https://mymuell.jumomind.com/mmapp/api.php?r=dates&city_id=%s&area_id=%s", id, id);

    int n = http_get(url, buf, PARSE_BUF);
    if (n <= 0) { free(buf); ESP_LOGW(TAG, "Abruf fehlgeschlagen"); return false; }

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(arr)) { cJSON_Delete(arr); ESP_LOGW(TAG, "kein JSON-Array"); return false; }

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

    publish(tmp, count);
    ESP_LOGI(TAG, "MyMuell: %d Termine geladen", count);
    return true;
}

// --- Quelle AWIDO (ICS) ------------------------------------------------------
typedef struct {
    char day[11];
    char title[24];
    bool local;      // LOCATION beginnt mit der gewaehlten Gemeinde
} ics_ev_t;

// ICS-Text-Escapes (\; \, \n \\) aufloesen, in-place.
static void ics_unescape(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            *o++ = (*p == 'n' || *p == 'N') ? ' ' : *p;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

// "Restmüll in Johannesberg" -> "Restmüll" (letztes " in " abschneiden).
static void strip_in_suffix(char *s)
{
    char *cut = NULL;
    for (char *p = strstr(s, " in "); p; p = strstr(p + 1, " in ")) cut = p;
    if (cut && cut != s) *cut = '\0';
}

// Parst eine ICS-Datei (gefaltete Zeilen werden zusammengefuegt) und haengt
// die Termine an ev[] an. Gibt die neue Anzahl zurueck.
static int parse_ics(char *buf, const char *ort, ics_ev_t *ev, int n)
{
    // 1) Zeilen entfalten: CRLF + (SPACE|TAB) entfernen, CR verwerfen.
    char *o = buf;
    for (char *p = buf; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n' && (p[1] == ' ' || p[1] == '\t')) { p++; continue; }
        *o++ = *p;
    }
    *o = '\0';

    size_t ort_len = strlen(ort);
    bool in_ev = false;
    char day[11] = "", summary[96] = "", location[96] = "";
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strcmp(line, "BEGIN:VEVENT") == 0) {
            in_ev = true; day[0] = summary[0] = location[0] = '\0';
        } else if (strcmp(line, "END:VEVENT") == 0) {
            if (in_ev && day[0] && summary[0] && n < ICS_MAX) {
                ics_unescape(summary);
                ics_unescape(location);
                strip_in_suffix(summary);
                copy_utf8(ev[n].day, sizeof(ev[n].day), day, strlen(day));
                copy_utf8(ev[n].title, sizeof(ev[n].title), summary, strlen(summary));
                ev[n].local = ort_len == 0 || strncasecmp(location, ort, ort_len) == 0;
                n++;
            }
            in_ev = false;
        } else if (in_ev && strncmp(line, "DTSTART", 7) == 0) {
            // DTSTART;TZID=Europe/Berlin;VALUE=DATE:20261216  bzw.  DTSTART:20261216T...
            const char *v = strrchr(line, ':');
            if (v && strlen(v + 1) >= 8) {
                v++;
                snprintf(day, sizeof(day), "%.4s-%.2s-%.2s", v, v + 4, v + 6);
            }
        } else if (in_ev && strncmp(line, "SUMMARY", 7) == 0) {
            const char *v = strchr(line, ':');
            if (v) snprintf(summary, sizeof(summary), "%s", v + 1);
        } else if (in_ev && strncmp(line, "LOCATION", 8) == 0) {
            const char *v = strchr(line, ':');
            if (v) snprintf(location, sizeof(location), "%s", v + 1);
        }
    }
    return n;
}

static int ev_cmp(const void *a, const void *b)
{
    const ics_ev_t *x = a, *y = b;
    int c = strcmp(x->day, y->day);
    return c ? c : strcmp(x->title, y->title);
}

static bool poll_awido(const char *today)
{
    if (!today[0]) return false;   // ohne Uhrzeit kein Jahr -> spaeter erneut

    char cl[40], id[48], ort[64];
    config_get_str_def("muell_aw_cl", cl, sizeof(cl), MUELL_DEF_AW_CL);
    config_get_str_def("muell_aw_id", id, sizeof(id), MUELL_DEF_AW_ID);
    config_get_str_def("muell_aw_ort", ort, sizeof(ort), "");
    if (!valid_client(cl) || !valid_guid(id)) {
        ESP_LOGW(TAG, "AWIDO: ungueltiger Mandant/ID ('%s' / '%s')", cl, id);
        return false;
    }

    char *buf = heap_caps_malloc(ICS_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ics_ev_t *ev = heap_caps_malloc(ICS_MAX * sizeof(ics_ev_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf || !ev) { free(buf); free(ev); ESP_LOGW(TAG, "AWIDO: kein Speicher"); return false; }

    // Laufendes Jahr; ab Oktober zusaetzlich das Folgejahr (Jahreswechsel).
    int year = atoi(today), month = atoi(today + 5);
    int years = month >= 10 ? 2 : 1;
    int n = 0;
    bool ok = false;
    for (int y = 0; y < years; y++) {
        char url[256];
        snprintf(url, sizeof(url),
                 AWIDO_BASE "/Customer/%s/KalenderICS.aspx?oid=%s&jahr=%d&fraktionen=&reminder=-1.17:00",
                 cl, id, year + y);
        int len = http_get(url, buf, ICS_BUF);
        if (len <= 0) {
            ESP_LOGW(TAG, "AWIDO: Abruf %d fehlgeschlagen", year + y);
            continue;
        }
        if (len >= ICS_BUF - 1) ESP_LOGW(TAG, "AWIDO: ICS %d abgeschnitten (>%d B)", year + y, ICS_BUF);
        if (y == 0) ok = true;   // Folgejahr darf fehlen (noch nicht veroeffentlicht)
        n = parse_ics(buf, ort, ev, n);
    }
    free(buf);
    if (!ok) { free(ev); return false; }

    // Nur Termine der gewaehlten Gemeinde (blendet z.B. die stationaere
    // Schadstoffsammlung Moembris aus) - ausser der Filter liesse nichts uebrig.
    int nlocal = 0;
    for (int i = 0; i < n; i++) if (ev[i].local) nlocal++;
    bool filter = nlocal > 0;

    qsort(ev, n, sizeof(ics_ev_t), ev_cmp);

    muell_entry_t tmp[MAX_ENTRIES];
    int count = 0;
    for (int i = 0; i < n && count < MAX_ENTRIES; i++) {
        if (filter && !ev[i].local) continue;
        if (strcmp(ev[i].day, today) < 0) continue;
        if (count > 0 && strcmp(tmp[count - 1].day, ev[i].day) == 0 &&
            strcmp(tmp[count - 1].title, ev[i].title) == 0) continue;   // Duplikat
        memcpy(tmp[count].day, ev[i].day, sizeof(tmp[0].day));
        memcpy(tmp[count].title, ev[i].title, sizeof(tmp[0].title));
        count++;
    }
    free(ev);

    publish(tmp, count);
    ESP_LOGI(TAG, "AWIDO %s: %d Termine geladen (%d roh, %d lokal)", cl, count, n, nlocal);
    return true;
}

int muell_awido_lookup(const char *step, const char *client, const char *id,
                       char *buf, size_t len)
{
    if (!step || !client || !valid_client(client)) return -1;
    char url[256];
    if (strcmp(step, "places") == 0) {
        snprintf(url, sizeof(url), AWIDO_BASE "/WebServices/Awido.Service.svc/secure/getPlaces/client=%s", client);
    } else if (strcmp(step, "streets") == 0 && id && valid_guid(id)) {
        snprintf(url, sizeof(url), AWIDO_BASE "/WebServices/Awido.Service.svc/secure/getGroupedStreets/%s?client=%s", id, client);
    } else if (strcmp(step, "addons") == 0 && id && valid_guid(id)) {
        snprintf(url, sizeof(url), AWIDO_BASE "/WebServices/Awido.Service.svc/secure/getStreetAddons/%s?client=%s", id, client);
    } else {
        return -1;
    }
    int n = http_get(url, buf, len);
    if (n >= (int)len - 1) { ESP_LOGW(TAG, "AWIDO-Liste zu gross (%s)", step); return -1; }
    return n;
}

// --- Poller ------------------------------------------------------------------
static bool poll_once(void)
{
    char today[11];
    time_today_str(today, sizeof(today));
    return s_src == MUELL_SRC_AWIDO ? poll_awido(today) : poll_mymuell(today);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));   // kurz warten (Netz/Uhr)
    for (;;) {
        bool ok = poll_once();
        // bis zum naechsten Turnus warten (nach Fehlschlag frueher) - oder
        // frueher aufwachen, wenn muell_refresh() benachrichtigt.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ok ? POLL_INTERVAL_MS : RETRY_MS));
    }
}

void muell_init(void)
{
    char src[12];
    config_get_str_def("muell_src", src, sizeof(src), MUELL_DEF_SRC);
    s_src = strcmp(src, "awido") == 0 ? MUELL_SRC_AWIDO : MUELL_SRC_MYMUELL;
    ESP_LOGI(TAG, "Datenquelle: %s", s_src == MUELL_SRC_AWIDO ? "AWIDO (Abfall-App)" : "MyMuell");

    s_lock = xSemaphoreCreateMutex();
    if (hc_mode() == HC_MODE_CLIENT) return;   // Termine kommen von home-central
    xTaskCreate(poll_task, "muell", 8192, NULL, 3, &s_task);   // 7168 war knapp (1224 B frei)
}

void muell_refresh(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

muell_src_t muell_source(void) { return s_src; }

void muell_status(char *out, size_t len)
{
    const char *name = hc_mode() == HC_MODE_CLIENT ? "home-central"
                     : s_src == MUELL_SRC_AWIDO ? "Abfall-App (AWIDO)" : "MyM\xc3\xbcll";
    if (!s_lock) { snprintf(out, len, "%s", name); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count;
    time_t t = s_last_ok;
    xSemaphoreGive(s_lock);
    if (t == 0) { snprintf(out, len, "Aktiv: %s &ndash; noch kein erfolgreicher Abruf", name); return; }
    struct tm tm; localtime_r(&t, &tm);
    snprintf(out, len, "Aktiv: %s &ndash; %d Termine, letzter Abruf %02d.%02d. %02d:%02d",
             name, n, tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
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

void muell_set(const muell_entry_t *list, int n)
{
    if (!s_lock) return;
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;
    publish(list, n);
}
