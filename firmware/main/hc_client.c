#include "hc_client.h"
#include "http_util.h"
#include "config_store.h"
#include "network_manager.h"
#include "time_sync.h"
#include "display.h"
#include "slides.h"
#include "slideshow.h"
#include "sysstatus.h"
#include "dht22.h"
#include "owm.h"
#include "spessart.h"
#include "dwd.h"
#include "nina.h"
#include "kurse.h"
#include "fritzbox.h"
#include "planes.h"
#include "space.h"
#include "muell.h"
#include "termine.h"
#include "feiertage.h"
#include "telegram.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "mdns.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

static const char *TAG = "hc";

#define SCREEN_BUF      (64 * 1024)          // /api/v1/screen (~10 KB, Reserve fuer Wachstum)
#define POLL_MS         (15 * 1000)          // Datenabruf
#define REPORT_EVERY    4                    // Statusmeldung jede 4. Runde (~60 s)
#define STALE_S         (5 * 60)             // ab hier "home-central nicht erreichbar"
#define REDISCOVER_S    (5 * 60)             // dann Adresse per mDNS neu suchen
#define PAIR_RETRY_MS   (30 * 1000)          // Such-/Kopplungsversuch im Direkt-Modus
#define FALLBACK_S      (30 * 60)            // so lange ohne home-central -> Notbetrieb (direkt)
#define PROBE_MS        (2 * 60 * 1000)      // im Notbetrieb: home-central wieder da?
#define CACHE_EVERY_S   (15 * 60)            // Daten-Cache hoechstens so oft in den Flash
#define CACHE_SLOT      (64 * 1024)          // ein Cache-Platz in der Partition "storage"
#define CACHE_SLOTS     64                   // Ring ueber 4 MB -> Flash-Verschleiss verteilt
#define CACHE_MAGIC     0x48434331u          // "HCC1"

static hc_mode_t s_mode;
static bool s_fallback;         // Notbetrieb: gekoppelt, aber home-central war zu lange weg
static char s_url[64];          // "http://192.168.79.1:80"
static char s_token[48];
static SemaphoreHandle_t s_lock;
static time_t s_last_ok;        // letzter erfolgreicher Datenabruf (Unix, 0 = nie)
static int  s_pair_state;       // 0 = nicht gesucht, 1 = nicht gefunden, 2 = wartet auf Freigabe
static char s_found_url[64];    // im Direkt-Modus gefundener Server

// --- Modus ---------------------------------------------------------------------
void hc_client_early_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    char src[12];
    config_get_str_def("data_src", src, sizeof(src), "auto");
    config_get_str_def("hc_url", s_url, sizeof(s_url), "");
    config_get_str_def("hc_token", s_token, sizeof(s_token), "");
    char fb[4] = "";
    config_get_str("hc_fallback", fb, sizeof(fb));
    bool paired = strcmp(src, "direct") != 0 && s_url[0] && s_token[0];
    s_fallback = paired && fb[0] == '1';
    s_mode = (paired && !s_fallback) ? HC_MODE_CLIENT : HC_MODE_DIRECT;
    ESP_LOGI(TAG, "Datenquelle: %s (data_src=%s%s%s)",
             s_mode == HC_MODE_CLIENT ? "home-central" : s_fallback ? "direkt (Notbetrieb)" : "direkt",
             src, s_url[0] ? ", Server " : "", s_url);
}

hc_mode_t hc_mode(void) { return s_mode; }

bool hc_client_ntp_host(char *out, size_t len)
{
    if (s_mode != HC_MODE_CLIENT) return false;
    const char *p = strstr(s_url, "://");
    p = p ? p + 3 : s_url;
    size_t i = 0;
    while (p[i] && p[i] != ':' && p[i] != '/' && i < len - 1) { out[i] = p[i]; i++; }
    out[i] = '\0';
    return i > 0;
}

// --- JSON-Helfer ---------------------------------------------------------------
static void jstr(const cJSON *o, const char *k, char *out, size_t len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    snprintf(out, len, "%s", cJSON_IsString(v) ? v->valuestring : "");
}
static double jnum(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static int jint(const cJSON *o, const char *k, int def) { return (int)jnum(o, k, def); }
static bool jbool(const cJSON *o, const char *k)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, k));
}
#define OBJ(o, k) cJSON_GetObjectItemCaseSensitive((o), (k))

// --- Daten verteilen -------------------------------------------------------------
static void fc_day(const cJSON *j, owm_fc_day_t *f)
{
    memset(f, 0, sizeof(*f));
    if (!cJSON_IsObject(j)) return;
    f->valid = jbool(j, "valid");
    jstr(j, "label", f->label, sizeof(f->label));
    f->tmin = jint(j, "tmin", 999); f->tmax = jint(j, "tmax", -999);
    jstr(j, "desc", f->desc, sizeof(f->desc));
    jstr(j, "icon", f->icon, sizeof(f->icon));
    f->rain_mm = (float)jnum(j, "rain_mm", 0); f->snow_mm = (float)jnum(j, "snow_mm", 0);
    f->gust_kmh = jint(j, "gust_kmh", 0); f->pop = jint(j, "pop", 0);
    f->thunder = jbool(j, "thunder"); f->freezing = jbool(j, "freezing"); f->fog = jbool(j, "fog");
    f->tmin_morning = jint(j, "tmin_morning", 999); f->rain_from = jint(j, "rain_from", -1);
}

static void apply_owm(const cJSON *j)
{
    static EXT_RAM_BSS_ATTR owm_data_t d;
    memset(&d, 0, sizeof(d));
    if (cJSON_IsObject(j)) {
        d.has_key = jbool(j, "has_key"); d.valid = jbool(j, "valid");
        jstr(j, "desc", d.desc, sizeof(d.desc)); jstr(j, "icon", d.icon, sizeof(d.icon));
        d.temp = jint(j, "temp", 0); d.feels = jint(j, "feels", 0);
        d.humidity = jint(j, "humidity", 0); d.wind_kmh = jint(j, "wind_kmh", 0);
        d.fog_now = jbool(j, "fog_now"); d.fc_valid = jbool(j, "fc_valid");
        const cJSON *fc = OBJ(j, "fc");
        for (int k = 0; k < 3; k++) fc_day(cJSON_GetArrayItem(fc, k), &d.fc[k]);
        fc_day(OBJ(j, "today"), &d.today);
    }
    owm_set(&d);
}

static void apply_spessart(const cJSON *j)
{
    spessart_data_t d; memset(&d, 0, sizeof(d));
    if (cJSON_IsObject(j)) {
        d.valid = jbool(j, "valid");
        jstr(j, "temp", d.temp, sizeof(d.temp)); jstr(j, "wind", d.wind, sizeof(d.wind));
        jstr(j, "gust", d.gust, sizeof(d.gust));
        d.has_stand = jbool(j, "has_stand");
        d.st_day = jint(j, "st_day", 0); d.st_mon = jint(j, "st_mon", 0); d.st_year = jint(j, "st_year", 0);
        d.st_hour = jint(j, "st_hour", 0); d.st_min = jint(j, "st_min", 0);
    }
    spessart_set(&d);
}

static void apply_warn(const cJSON *jd, const cJSON *jn)
{
    dwd_data_t d; memset(&d, 0, sizeof(d));
    if (cJSON_IsObject(jd)) {
        d.valid = jbool(jd, "valid"); d.count = jint(jd, "count", 0);
        jstr(jd, "headline", d.headline, sizeof(d.headline));
        jstr(jd, "severity", d.severity, sizeof(d.severity));
    }
    dwd_set(&d);
    nina_data_t n; memset(&n, 0, sizeof(n));
    if (cJSON_IsObject(jn)) {
        n.valid = jbool(jn, "valid"); n.count = jint(jn, "count", 0);
        jstr(jn, "headline", n.headline, sizeof(n.headline));
        jstr(jn, "severity", n.severity, sizeof(n.severity));
        jstr(jn, "provider", n.provider, sizeof(n.provider));
    }
    nina_set(&n);
}

static void apply_kurse(const cJSON *j)
{
    kurse_data_t d; memset(&d, 0, sizeof(d));
    if (cJSON_IsObject(j)) {
        d.valid = jbool(j, "valid"); d.has_trend = jbool(j, "has_trend");
        d.btc_eur = jnum(j, "btc_eur", 0); d.eur_usd = jnum(j, "eur_usd", 0);
        d.btc_chg = jnum(j, "btc_chg", 0); d.eur_chg = jnum(j, "eur_chg", 0);
    }
    kurse_set(&d);
}

static void apply_fritz(const cJSON *j)
{
    fritzbox_data_t d; memset(&d, 0, sizeof(d));
    if (cJSON_IsObject(j)) {
        d.reachable = jbool(j, "reachable");
        jstr(j, "external_ip", d.external_ip, sizeof(d.external_ip));
        d.down_max_bps = (uint32_t)jnum(j, "down_max_bps", 0);
        d.up_max_bps = (uint32_t)jnum(j, "up_max_bps", 0);
        d.down_rate_Bps = (uint32_t)jnum(j, "down_rate_Bps", 0);
        d.up_rate_Bps = (uint32_t)jnum(j, "up_rate_Bps", 0);
    }
    fritzbox_set(&d);
}

static void apply_planes(const cJSON *j)
{
    static EXT_RAM_BSS_ATTR planes_data_t d;
    memset(&d, 0, sizeof(d));
    if (cJSON_IsObject(j)) {
        d.valid = jbool(j, "valid");
        const cJSON *a;
        cJSON_ArrayForEach(a, OBJ(j, "ac")) {
            if (d.count >= PLANES_MAX) break;
            plane_t *p = &d.ac[d.count++];
            jstr(a, "flight", p->flight, sizeof(p->flight)); jstr(a, "type", p->type, sizeof(p->type));
            jstr(a, "reg", p->reg, sizeof(p->reg));
            p->alt_ft = jint(a, "alt_ft", 0); p->gs_kt = jint(a, "gs_kt", 0); p->track = jint(a, "track", -1);
            p->dst_nm = jnum(a, "dst_nm", 9999);
            jstr(a, "from", p->from, sizeof(p->from)); jstr(a, "to", p->to, sizeof(p->to));
        }
    }
    planes_set(&d);
}

static void apply_space(const cJSON *j)
{
    static EXT_RAM_BSS_ATTR sat_passes_t sp;
    static EXT_RAM_BSS_ATTR neos_t ne;
    memset(&sp, 0, sizeof(sp)); memset(&ne, 0, sizeof(ne));
    const cJSON *jp = OBJ(j, "passes"), *jn = OBJ(j, "neos"), *e;
    if (cJSON_IsObject(jp)) {
        sp.has_key = jbool(jp, "has_key"); sp.valid = jbool(jp, "valid");
        cJSON_ArrayForEach(e, OBJ(jp, "p")) {
            if (sp.count >= SPACE_MAX_PASSES) break;
            sat_pass_t *p = &sp.p[sp.count++];
            jstr(e, "name", p->name, sizeof(p->name)); jstr(e, "dir", p->dir, sizeof(p->dir));
            p->max_utc = (time_t)jnum(e, "max_utc", 0); p->max_el = jint(e, "max_el", 0);
            p->mag = jnum(e, "mag", 99.0);
        }
    }
    if (cJSON_IsObject(jn)) {
        ne.has_key = jbool(jn, "has_key"); ne.valid = jbool(jn, "valid");
        cJSON_ArrayForEach(e, OBJ(jn, "n")) {
            if (ne.count >= SPACE_MAX_NEOS) break;
            neo_t *o = &ne.n[ne.count++];
            jstr(e, "name", o->name, sizeof(o->name));
            o->miss_km = jnum(e, "miss_km", 1e12); o->diam_m = jnum(e, "diam_m", 0);
            o->hazard = jbool(e, "hazard"); o->approach_utc = (time_t)jnum(e, "approach_utc", 0);
        }
    }
    space_set(&sp, &ne);
}

static void apply_lists(const cJSON *root)
{
    static EXT_RAM_BSS_ATTR muell_entry_t me[48];
    static EXT_RAM_BSS_ATTR termine_entry_t te[50];
    static EXT_RAM_BSS_ATTR feiertag_t fe[40];
    static EXT_RAM_BSS_ATTR tg_msg_t tm[TG_MAX_MSGS];
    const cJSON *e;
    int n = 0;
    cJSON_ArrayForEach(e, OBJ(root, "muell")) {
        if (n >= 48) break;
        jstr(e, "day", me[n].day, sizeof(me[n].day)); jstr(e, "title", me[n].title, sizeof(me[n].title)); n++;
    }
    muell_set(me, n);
    n = 0;
    cJSON_ArrayForEach(e, OBJ(root, "termine")) {
        if (n >= 50) break;
        jstr(e, "date", te[n].date, sizeof(te[n].date)); jstr(e, "time", te[n].time, sizeof(te[n].time));
        jstr(e, "title", te[n].title, sizeof(te[n].title)); n++;
    }
    termine_set_remote(te, n);
    n = 0;
    cJSON_ArrayForEach(e, OBJ(root, "feiertage")) {
        if (n >= 40) break;
        jstr(e, "date", fe[n].date, sizeof(fe[n].date)); jstr(e, "name", fe[n].name, sizeof(fe[n].name)); n++;
    }
    feiertage_set_remote(fe, n);
    const cJSON *tg = OBJ(root, "telegram");
    n = 0;
    cJSON_ArrayForEach(e, OBJ(tg, "msgs")) {
        if (n >= TG_MAX_MSGS) break;
        jstr(e, "from", tm[n].from, sizeof(tm[n].from)); jstr(e, "text", tm[n].text, sizeof(tm[n].text)); n++;
    }
    telegram_set_remote(tm, n, (unsigned)jnum(tg, "version", 0), jbool(tg, "configured"));
}

// Anzeige-Einstellungen von home-central. Helligkeit wirkt sofort; Drehung,
// Intervall und Slide-Auswahl werden beim Start aufgebaut -> speichern + Neustart.
static void apply_display(const cJSON *j)
{
    if (!cJSON_IsObject(j)) return;
    int floor_new = jint(j, "bl_floor", -1), bright_new = jint(j, "brightness", -1);
    bool bl_changed = false;
    if (floor_new >= 0 && floor_new != config_get_int("bl_floor", 80)) { config_set_int("bl_floor", floor_new); bl_changed = true; }
    if (bright_new > 0 && bright_new != config_get_int("brightness", 100)) { config_set_int("brightness", bright_new); bl_changed = true; }
    if (bl_changed) display_set_brightness(config_get_int("brightness", 100));

    bool restart = false;
    int sec = jint(j, "slide_sec", 0);
    if (sec >= 3 && sec <= 120 && sec != config_get_int("slide_sec", 10)) {
        config_set_int("slide_sec", sec);
        restart = config_get_int("slide_sec", 10) == sec;
    }
    const cJSON *rot = OBJ(j, "rot180");
    if (cJSON_IsBool(rot)) {
        char cur[4] = "0"; config_get_str("rot180", cur, sizeof(cur));
        const char *want = cJSON_IsTrue(rot) ? "1" : "0";
        if (strcmp(cur, want) != 0) { config_set_str("rot180", want); restart = true; }
    }
    const cJSON *sl = OBJ(j, "slides");
    for (int i = 0; cJSON_IsObject(sl) && i < slides_catalog_count(); i++) {
        const cJSON *v = OBJ(sl, slides_catalog_id(i));
        if (!cJSON_IsBool(v)) continue;            // unbekannt -> Geraete-Default behalten
        bool want = cJSON_IsTrue(v);
        if (want != slides_catalog_enabled(i)) {
            char key[16]; snprintf(key, sizeof(key), "sl_%s", slides_catalog_id(i));
            config_set_str(key, want ? "1" : "0");
            restart = true;
        }
    }
    if (restart) {
        ESP_LOGW(TAG, "Anzeige-Einstellungen von home-central geaendert -> Neustart");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}

// Server-Antwort uebernehmen. from_cache: gesicherte Antwort nach dem Start ohne
// Server -> nur Daten, KEINE Uhrzeit (veraltet), kein Kommando, keine Anzeige.
// cfg_hash (optional) erhaelt den Pruefwert der Einstellungen.
static bool apply_screen(const char *json, bool from_cache, char *cfg_hash, size_t hlen)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }

    // Uhr: normal per NTP von home-central; ohne Sync die Serverzeit uebernehmen.
    double t = jnum(root, "time", 0);
    if (!from_cache && t > 1.7e9 && !time_sync_is_valid()) {
        struct timeval tv = { .tv_sec = (time_t)t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Uhr von home-central gestellt");
    }
    apply_owm(OBJ(root, "owm"));
    apply_spessart(OBJ(root, "spessart"));
    apply_warn(OBJ(root, "dwd"), OBJ(root, "nina"));
    apply_kurse(OBJ(root, "kurse"));
    apply_fritz(OBJ(root, "fritzbox"));
    apply_planes(OBJ(root, "planes"));
    apply_space(OBJ(root, "space"));
    apply_lists(root);
    if (cfg_hash) jstr(root, "cfg_hash", cfg_hash, hlen);

    const cJSON *cmd = OBJ(root, "cmd");
    bool reboot = !from_cache && cJSON_IsString(cmd) && strcmp(cmd->valuestring, "reboot") == 0;
    if (!from_cache) apply_display(OBJ(root, "display"));
    cJSON_Delete(root);
    if (reboot) {
        ESP_LOGW(TAG, "Neustart auf Anforderung von home-central");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    return true;
}

// --- Einstellungs-Spiegel (fuer den Notbetrieb ohne home-central) ----------------
// Schluessel, die nie aus home-central uebernommen werden.
static bool mirror_skip(const char *k)
{
    static const char *SKIP[] = { "data_src", "hc_url", "hc_token", "hc_cfg_hash", "hc_fallback",
                                  "wifi_ssid", "wifi_pass", "dev_name", "rot180", "brightness",
                                  "bl_floor", "slide_sec" };
    for (size_t i = 0; i < sizeof(SKIP) / sizeof(SKIP[0]); i++) if (strcmp(k, SKIP[i]) == 0) return true;
    return strncmp(k, "sl_", 3) == 0;
}

static void sync_config(const char *want_hash)
{
    char *buf = heap_caps_malloc(SCREEN_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *cur = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf || !cur) { free(buf); free(cur); return; }
    char url[96]; snprintf(url, sizeof(url), "%s/api/v1/config", s_url);
    int status = 0;
    int n = http_request(url, NULL, s_token, buf, SCREEN_BUF, &status);
    cJSON *root = (n > 0 && status == 200) ? cJSON_Parse(buf) : NULL;
    const cJSON *cfg = OBJ(root, "config"), *e;
    int changed = 0;
    if (cJSON_IsObject(cfg)) {
        cJSON_ArrayForEach(e, cfg) {
            if (!cJSON_IsString(e) || !e->string || mirror_skip(e->string)) continue;
            if (strlen(e->string) > 15 || strlen(e->valuestring) >= 4000) continue;   // NVS-Grenzen
            if (config_get_str(e->string, cur, 4096) && strcmp(cur, e->valuestring) == 0) continue;
            config_set_str(e->string, e->valuestring);   // nur Aenderungen schreiben (Flash schonen)
            changed++;
        }
        config_set_str("hc_cfg_hash", want_hash);
        ESP_LOGI(TAG, "Einstellungs-Kopie von home-central aktualisiert (%d geaendert)", changed);
    } else {
        ESP_LOGW(TAG, "Einstellungs-Kopie: HTTP %d", status);
    }
    cJSON_Delete(root);
    free(buf); free(cur);
}

// --- Daten-Cache im Flash (Partition "storage", Ring aus 64 Plaetzen) -------------
typedef struct { uint32_t magic, seq, len, crc; } cache_hdr_t;
static const esp_partition_t *s_cache_part;
static uint32_t s_cache_seq;
static int s_cache_slot = -1;

static void cache_init(void)
{
    s_cache_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (!s_cache_part || s_cache_part->size < (size_t)CACHE_SLOT * CACHE_SLOTS) {
        ESP_LOGW(TAG, "Cache-Partition 'storage' fehlt/zu klein - kein Daten-Cache");
        s_cache_part = NULL;
        return;
    }
    for (int i = 0; i < CACHE_SLOTS; i++) {   // neuesten gueltigen Platz suchen
        cache_hdr_t h;
        if (esp_partition_read(s_cache_part, (size_t)i * CACHE_SLOT, &h, sizeof(h)) != ESP_OK) continue;
        if (h.magic != CACHE_MAGIC || h.len == 0 || h.len > CACHE_SLOT - sizeof(h)) continue;
        if (s_cache_slot < 0 || h.seq > s_cache_seq) { s_cache_seq = h.seq; s_cache_slot = i; }
    }
}

static bool cache_load(char *buf, size_t len)
{
    if (!s_cache_part || s_cache_slot < 0) return false;
    cache_hdr_t h;
    size_t off = (size_t)s_cache_slot * CACHE_SLOT;
    if (esp_partition_read(s_cache_part, off, &h, sizeof(h)) != ESP_OK || h.len >= len) return false;
    if (esp_partition_read(s_cache_part, off + sizeof(h), buf, h.len) != ESP_OK) return false;
    buf[h.len] = '\0';
    return esp_rom_crc32_le(0, (const uint8_t *)buf, h.len) == h.crc;
}

static void cache_save(const char *json, size_t n)
{
    if (!s_cache_part || n == 0 || n > CACHE_SLOT - sizeof(cache_hdr_t)) return;
    int slot = (s_cache_slot + 1) % CACHE_SLOTS;
    size_t off = (size_t)slot * CACHE_SLOT;
    size_t span = (sizeof(cache_hdr_t) + n + 4095) & ~(size_t)4095;   // nur benoetigte Sektoren loeschen
    cache_hdr_t h = { CACHE_MAGIC, s_cache_seq + 1, (uint32_t)n,
                      esp_rom_crc32_le(0, (const uint8_t *)json, n) };
    if (esp_partition_erase_range(s_cache_part, off, span) != ESP_OK) return;
    if (esp_partition_write(s_cache_part, off + sizeof(h), json, n) != ESP_OK) return;
    if (esp_partition_write(s_cache_part, off, &h, sizeof(h)) != ESP_OK) return;   // Kopf zuletzt
    s_cache_slot = slot;
    s_cache_seq = h.seq;
}

// --- Statusmeldung an home-central ----------------------------------------------
static void send_report(void)
{
    cJSON *r = cJSON_CreateObject();
    const esp_app_desc_t *app = esp_app_get_description();
    char name[32]; config_get_str_def("dev_name", name, sizeof(name), "esp-infoscreen");
    cJSON_AddStringToObject(r, "name", name);
    cJSON_AddStringToObject(r, "fw", app ? app->version : "?");

    dht22_data_t d; dht22_get(&d);
    cJSON *jd = cJSON_AddObjectToObject(r, "dht22");
    cJSON_AddBoolToObject(jd, "valid", d.valid);
    cJSON_AddNumberToObject(jd, "temp_c", d.temp_c);
    cJSON_AddNumberToObject(jd, "hum_pct", d.hum_pct);
    float ht[DHT22_HIST_LEN], hh[DHT22_HIST_LEN];
    int hn = dht22_history_get(ht, hh, DHT22_HIST_LEN);
    cJSON *jh = cJSON_AddObjectToObject(r, "dht22_hist");
    cJSON *at = cJSON_AddArrayToObject(jh, "temp"), *ah = cJSON_AddArrayToObject(jh, "hum");
    for (int i = 0; i < hn; i++) {
        cJSON_AddItemToArray(at, cJSON_CreateNumber(ht[i]));
        cJSON_AddItemToArray(ah, cJSON_CreateNumber(hh[i]));
    }

    net_status_t st; network_manager_get_status(&st);
    cJSON_AddStringToObject(r, "ssid", st.ssid);
    cJSON_AddStringToObject(r, "dev_ip", st.ip);
    cJSON_AddNumberToObject(r, "rssi", st.rssi);
    cJSON_AddNumberToObject(r, "rssi_avg", network_manager_get_avg_rssi());

    char *txt = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (txt) {
        sysstatus_text(txt, 1024);
        cJSON_AddStringToObject(r, "status", txt);
        sysstatus_stacks(txt, 1024);
        cJSON_AddStringToObject(r, "stacks", txt);
        free(txt);
    }
    // Slide-Katalog: home-central zeigt damit auch kuenftige Slides in der Auswahl
    cJSON *cat = cJSON_AddArrayToObject(r, "slides");
    for (int i = 0; i < slides_catalog_count(); i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "id", slides_catalog_id(i));
        cJSON_AddStringToObject(s, "title", slides_catalog_title(i));
        cJSON_AddItemToArray(cat, s);
    }

    char *body = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (!body) return;
    char url[96]; snprintf(url, sizeof(url), "%s/api/v1/report", s_url);
    char resp[64]; int status;
    http_request(url, body, s_token, resp, sizeof(resp), &status);
    cJSON_free(body);
    if (status != 200) ESP_LOGW(TAG, "Statusmeldung: HTTP %d", status);
}

// --- Suche (mDNS) + Kopplung ------------------------------------------------------
static bool discover(char *url, size_t len)
{
    mdns_result_t *res = NULL;
    if (mdns_query_ptr("_home-central", "_tcp", 3000, 4, &res) == ESP_OK && res) {
        for (mdns_result_t *r = res; r; r = r->next) {
            for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
                if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                    snprintf(url, len, "http://" IPSTR ":%u", IP2STR(&a->addr.u_addr.ip4), (unsigned)r->port);
                    mdns_query_results_free(res);
                    return true;
                }
            }
        }
        mdns_query_results_free(res);
    }
    esp_ip4_addr_t ip;   // Fallback: Hostname home-central.local
    if (mdns_query_a("home-central", 2000, &ip) == ESP_OK) {
        snprintf(url, len, "http://" IPSTR ":80", IP2STR(&ip));
        return true;
    }
    return false;
}

// Kopplung anfragen. true + token, wenn freigegeben; false = (noch) nicht.
static bool request_pair(const char *url, char *token, size_t tlen)
{
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char name[32]; config_get_str_def("dev_name", name, sizeof(name), "esp-infoscreen");
    const esp_app_desc_t *app = esp_app_get_description();
    char body[160];
    snprintf(body, sizeof(body), "{\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"name\":\"%s\",\"fw\":\"%s\"}",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], name, app ? app->version : "?");
    char ep[96]; snprintf(ep, sizeof(ep), "%s/api/v1/pair", url);
    char resp[160]; int status;
    if (http_request(ep, body, NULL, resp, sizeof(resp), &status) < 0 || status != 200) return false;
    cJSON *j = cJSON_Parse(resp);
    bool ok = false;
    const cJSON *st = OBJ(j, "status"), *tk = OBJ(j, "token");
    if (cJSON_IsString(st) && strcmp(st->valuestring, "ok") == 0 && cJSON_IsString(tk) && tk->valuestring[0]) {
        snprintf(token, tlen, "%s", tk->valuestring);
        ok = true;
    }
    cJSON_Delete(j);
    return ok;
}

static bool wait_connected(void)
{
    net_status_t st; network_manager_get_status(&st);
    return st.connected;
}

// Direkt-Modus mit data_src auto/hc: home-central suchen und Kopplung anfragen.
static void pair_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10000));
    for (;;) {
        if (wait_connected()) {
            char url[64], token[48];
            if (!discover(url, sizeof(url))) {
                s_pair_state = 1;
            } else {
                snprintf(s_found_url, sizeof(s_found_url), "%s", url);
                if (request_pair(url, token, sizeof(token))) {
                    config_set_str("hc_url", url);
                    config_set_str("hc_token", token);
                    ESP_LOGW(TAG, "Von home-central (%s) freigegeben -> Neustart im Client-Modus", url);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
                if (s_pair_state != 2) ESP_LOGI(TAG, "home-central gefunden (%s), warte auf Freigabe", url);
                s_pair_state = 2;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PAIR_RETRY_MS));
    }
}

// --- Client-Modus ------------------------------------------------------------------
static void client_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(SCREEN_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { ESP_LOGE(TAG, "kein Speicher fuer den Abruf"); vTaskDelete(NULL); return; }

    // Gesicherte Daten anzeigen, bis home-central antwortet (Start ohne Server).
    cache_init();
    if (cache_load(buf, SCREEN_BUF) && apply_screen(buf, true, NULL, 0))
        ESP_LOGI(TAG, "Daten aus dem Cache geladen (Stand vor dem Neustart)");

    vTaskDelay(pdMS_TO_TICKS(3000));
    int round = 0, fails = 0;
    uint32_t last_ok_ms = 0, last_cache_ms = 0, last_discover_ms = 0;
    bool stale_shown = false, have_cache_time = false;
    for (;;) {
        uint32_t now_ms = esp_log_timestamp();
        if (wait_connected()) {
            char url[96]; snprintf(url, sizeof(url), "%s/api/v1/screen", s_url);
            int status = 0;
            char hash[16] = "";
            int n = http_request(url, NULL, s_token, buf, SCREEN_BUF, &status);
            if (n >= SCREEN_BUF - 1) ESP_LOGW(TAG, "Antwort >= %d B - SCREEN_BUF vergroessern", SCREEN_BUF);
            if (n > 0 && status == 200 && apply_screen(buf, false, hash, sizeof(hash))) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_last_ok = time(NULL);
                xSemaphoreGive(s_lock);
                last_ok_ms = now_ms;
                fails = 0;
                if (!have_cache_time || now_ms - last_cache_ms >= CACHE_EVERY_S * 1000U) {
                    cache_save(buf, (size_t)n);   // fuer den naechsten Start ohne Server
                    last_cache_ms = now_ms;
                    have_cache_time = true;
                }
                char have[16] = "";
                config_get_str("hc_cfg_hash", have, sizeof(have));
                if (hash[0] && strcmp(hash, have) != 0) sync_config(hash);
                if (round % REPORT_EVERY == 0) send_report();
                round++;
            } else {
                fails++;
                ESP_LOGW(TAG, "Abruf fehlgeschlagen (HTTP %d, %d. Mal)", status, fails);
                if (status == 401) {   // Token erneuert? -> per Kopplung neu holen (MAC ist freigegeben)
                    char token[48];
                    if (request_pair(s_url, token, sizeof(token))) {
                        snprintf(s_token, sizeof(s_token), "%s", token);
                        config_set_str("hc_token", token);
                        ESP_LOGI(TAG, "Neuer Token von home-central uebernommen");
                    }
                }
                if (fails >= 3 && now_ms - last_discover_ms > REDISCOVER_S * 1000U) {   // Server umgezogen?
                    last_discover_ms = now_ms;
                    char nu[64];
                    if (discover(nu, sizeof(nu)) && strcmp(nu, s_url) != 0) {
                        ESP_LOGW(TAG, "home-central unter neuer Adresse %s", nu);
                        snprintf(s_url, sizeof(s_url), "%s", nu);
                        config_set_str("hc_url", nu);
                    }
                }
            }
        }
        // Wie lange schon ohne home-central? (Uptime-basiert: unabhaengig von der Uhr)
        uint32_t gone_s = (now_ms - last_ok_ms) / 1000U;
        if (gone_s >= FALLBACK_S && wait_connected()) {   // ohne WLAN hilft der Notbetrieb nicht
            ESP_LOGW(TAG, "home-central seit %u min nicht erreichbar -> Notbetrieb (direkte Abfragen)",
                     (unsigned)(gone_s / 60));
            config_set_str("hc_fallback", "1");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
        bool stale = gone_s >= STALE_S;
        if (stale != stale_shown) {
            stale_shown = stale;
            if (stale) {
                char msg[96];
                time_t ok;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                ok = s_last_ok;
                xSemaphoreGive(s_lock);
                if (ok) { struct tm tm; localtime_r(&ok, &tm);
                          snprintf(msg, sizeof(msg), "home-central nicht erreichbar - Daten von %02d:%02d", tm.tm_hour, tm.tm_min); }
                else      snprintf(msg, sizeof(msg), "home-central nicht erreichbar - gespeicherte Daten");
                slideshow_set_status(msg);
            } else {
                slideshow_set_status(NULL);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

// Notbetrieb: direkte Abfragen laufen; regelmaessig pruefen, ob home-central
// wieder da ist -> dann zurueck in den Client-Modus (Neustart).
static void probe_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(15000));
    slideshow_set_status("Notbetrieb: home-central nicht erreichbar - direkte Abfragen");
    for (;;) {
        if (wait_connected()) {
            char url[96], resp[128]; int status = 0;
            snprintf(url, sizeof(url), "%s/api/v1/health", s_url);
            http_request(url, NULL, NULL, resp, sizeof(resp), &status);
            if (status != 200) {   // evtl. neue Adresse
                char nu[64];
                if (discover(nu, sizeof(nu))) {
                    snprintf(url, sizeof(url), "%s/api/v1/health", nu);
                    http_request(url, NULL, NULL, resp, sizeof(resp), &status);
                    if (status == 200 && strcmp(nu, s_url) != 0) config_set_str("hc_url", nu);
                }
            }
            if (status == 200) {
                ESP_LOGW(TAG, "home-central wieder erreichbar -> zurueck in den Client-Modus");
                config_set_str("hc_fallback", "0");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PROBE_MS));
    }
}

void hc_client_start(void)
{
    if (s_mode == HC_MODE_CLIENT) {
        xTaskCreate(client_task, "hc_client", 8192, NULL, 4, NULL);
        return;
    }
    if (s_fallback) {
        xTaskCreate(probe_task, "hc_probe", 6144, NULL, 2, NULL);
        return;
    }
    char src[12]; config_get_str_def("data_src", src, sizeof(src), "auto");
    if (strcmp(src, "direct") != 0) xTaskCreate(pair_task, "hc_pair", 6144, NULL, 2, NULL);
}

void hc_client_status(char *out, size_t len)
{
    char src[12]; config_get_str_def("data_src", src, sizeof(src), "auto");
    if (s_mode == HC_MODE_CLIENT) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        time_t ok = s_last_ok;
        xSemaphoreGive(s_lock);
        if (!ok) { snprintf(out, len, "home-central (%s) &ndash; noch kein Abruf", s_url); return; }
        struct tm tm; localtime_r(&ok, &tm);
        snprintf(out, len, "home-central (%s) &ndash; letzter Abruf %02d:%02d:%02d", s_url, tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else if (s_fallback) {
        snprintf(out, len, "<b>Notbetrieb</b>: home-central (%s) nicht erreichbar &ndash; direkte Abfragen "
                 "mit der gespiegelten Einstellungs-Kopie; Pr&uuml;fung alle 2 min", s_url);
    } else if (strcmp(src, "direct") == 0) {
        snprintf(out, len, "Direkte Abfragen (home-central-Suche aus)");
    } else if (s_pair_state == 2) {
        snprintf(out, len, "Direkte Abfragen &ndash; home-central gefunden (%s), <b>bitte dort freigeben</b>", s_found_url);
    } else if (s_pair_state == 1) {
        snprintf(out, len, "Direkte Abfragen &ndash; home-central im Netz nicht gefunden (sucht weiter)");
    } else {
        snprintf(out, len, "Direkte Abfragen &ndash; suche home-central &hellip;");
    }
}

bool hc_client_tg_send(const char *text)
{
    if (s_mode != HC_MODE_CLIENT) return false;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "text", text);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) return false;
    char url[96]; snprintf(url, sizeof(url), "%s/api/v1/tg/send", s_url);
    char resp[64]; int status;
    http_request(url, body, s_token, resp, sizeof(resp), &status);
    cJSON_free(body);
    return status == 200;
}
