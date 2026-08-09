#include "telegram.h"
#include "http_util.h"
#include "config_store.h"
#include "spessart.h"
#include "fritzbox.h"
#include "termine.h"
#include "muell.h"
#include "dwd.h"
#include "nina.h"
#include "sysstatus.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "telegram";
#define API "https://api.telegram.org/bot"

// --- Zustand ----------------------------------------------------------------
static SemaphoreHandle_t s_lock;
static EXT_RAM_BSS_ATTR tg_msg_t s_hist[TG_MAX_MSGS];   // PSRAM
static int      s_hist_count;
static int      s_hist_head;      // Index der aeltesten Nachricht
static unsigned s_version;        // erhoeht sich bei jeder neuen Nachricht

static char     s_botname[40];    // Username des Bots (von getMe), klein
static long long s_offset;        // getUpdates-Offset (naechste update_id)
static bool     s_primed;         // Startabgleich erledigt?
static bool     s_greeted;        // Boot-Meldung gesendet?
static char     s_last_warn[96];  // zuletzt gepostete DWD-Warnungs-Kopfzeile
static char     s_last_nina[96];  // zuletzt gepostete BBK/NINA-Warnung

// --- kleine Helfer ----------------------------------------------------------
static void tolower_str(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

// Enthaelt haystack (schon klein) das (kleine) needle?
static bool contains_ci(const char *hay_lower, const char *needle_lower)
{
    return strstr(hay_lower, needle_lower) != NULL;
}

// URL-Encoding (RFC 3986, reservierte Zeichen + Nicht-ASCII als %XX).
static void urlencode(const char *in, char *out, size_t out_len)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 4 < out_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

// --- Verlauf ----------------------------------------------------------------
static void hist_add(const char *from, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = (s_hist_head + s_hist_count) % TG_MAX_MSGS;
    if (s_hist_count == TG_MAX_MSGS) {          // voll -> aelteste ueberschreiben
        idx = s_hist_head;
        s_hist_head = (s_hist_head + 1) % TG_MAX_MSGS;
    } else {
        s_hist_count++;
    }
    snprintf(s_hist[idx].from, sizeof(s_hist[idx].from), "%s", from && from[0] ? from : "?");
    snprintf(s_hist[idx].text, sizeof(s_hist[idx].text), "%s", text ? text : "");
    s_version++;
    xSemaphoreGive(s_lock);
}

int telegram_get_history(tg_msg_t *out, int max)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_hist_count < max ? s_hist_count : max;
    // die neuesten n Nachrichten, chronologisch
    int start = s_hist_count - n;
    for (int i = 0; i < n; i++) {
        int idx = (s_hist_head + start + i) % TG_MAX_MSGS;
        out[i] = s_hist[idx];
    }
    xSemaphoreGive(s_lock);
    return n;
}

unsigned telegram_history_version(void)
{
    return s_version;
}

// --- Konfig -----------------------------------------------------------------
static bool get_token(char *out, size_t len)
{
    return config_get_str("tg_token", out, len) && out[0] != '\0';
}
static bool get_chat(char *out, size_t len)
{
    return config_get_str("tg_chat", out, len) && out[0] != '\0';
}

bool telegram_configured(void)
{
    char t[64], c[32];
    return get_token(t, sizeof(t)) && get_chat(c, sizeof(c));
}

// --- Senden -----------------------------------------------------------------
bool telegram_send(const char *text)
{
    char token[64], chat[32];
    if (!get_token(token, sizeof(token)) || !get_chat(chat, sizeof(chat))) return false;

    char enc[1024];
    urlencode(text, enc, sizeof(enc));
    static char url[1400];
    snprintf(url, sizeof(url), "%s%s/sendMessage?chat_id=%s&text=%s", API, token, chat, enc);

    static char resp[512];
    int n = http_get(url, resp, sizeof(resp));
    if (n <= 0) { ESP_LOGW(TAG, "sendMessage fehlgeschlagen"); return false; }
    return true;
}

// --- Antwort-Bausteine (UTF-8, mit Umlauten) --------------------------------
static void build_weather(char *out, size_t len)
{
    spessart_data_t s; spessart_get(&s);
    if (s.valid)
        snprintf(out, len, "\xF0\x9F\x8C\xA1 Spessartwetter: %s \xC2\xB0""C, Wind %s km/h",
                 s.temp[0] ? s.temp : "?", s.wind[0] ? s.wind : "?");
    else
        snprintf(out, len, "Wetterdaten noch nicht verf\xC3\xBCgbar.");
}

static void build_internet(char *out, size_t len)
{
    fritzbox_data_t d; fritzbox_get(&d);
    if (!d.reachable) { snprintf(out, len, "Fritzbox nicht erreichbar."); return; }
    uint32_t dn10 = (uint32_t)((uint64_t)d.down_rate_Bps * 8 / 100000);
    uint32_t up10 = (uint32_t)((uint64_t)d.up_rate_Bps   * 8 / 100000);
    uint32_t dnmax = d.down_max_bps / 1000000;
    uint32_t upmax = d.up_max_bps   / 1000000;
    snprintf(out, len,
        "\xF0\x9F\x8C\x90 Internet: IP %s | Down %u.%u/%u Mbit/s | Up %u.%u/%u Mbit/s",
        d.external_ip[0] ? d.external_ip : "-",
        (unsigned)(dn10 / 10), (unsigned)(dn10 % 10), (unsigned)dnmax,
        (unsigned)(up10 / 10), (unsigned)(up10 % 10), (unsigned)upmax);
}

// naechste zwei Termine (Nutzer-Termine + Muellabfuhr), nach Datum sortiert
static void build_termine(char *out, size_t len)
{
    // static: build_termine laeuft nur im Telegram-Task (nicht reentrant) ->
    // haelt den Task-Stack frei (grosse Arrays sonst = Stack-Overflow).
    typedef struct { char date[11]; char label[64]; } ev_t;
    static EXT_RAM_BSS_ATTR ev_t ev[100];   // PSRAM
    int n = 0;
    char today[16] = { 0 };
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_year > 120) strftime(today, sizeof(today), "%Y-%m-%d", &tm);

    static EXT_RAM_BSS_ATTR termine_entry_t te[50];   // PSRAM
    int tn = termine_get_all(te, 50);
    for (int i = 0; i < tn && n < 100; i++) {
        if (today[0] && strcmp(te[i].date, today) < 0) continue;
        strncpy(ev[n].date, te[i].date, 10); ev[n].date[10] = '\0';
        if (te[i].time[0]) snprintf(ev[n].label, sizeof(ev[n].label), "%s %s", te[i].time, te[i].title);
        else               snprintf(ev[n].label, sizeof(ev[n].label), "%s", te[i].title);
        n++;
    }
    static EXT_RAM_BSS_ATTR muell_entry_t me[48];   // PSRAM
    int mn = muell_get(me, 48);
    for (int i = 0; i < mn && n < 100; i++) {
        strncpy(ev[n].date, me[i].day, 10); ev[n].date[10] = '\0';
        snprintf(ev[n].label, sizeof(ev[n].label), "Abfuhr: %s", me[i].title);
        n++;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(ev[j].date, ev[i].date) < 0) { ev_t t = ev[i]; ev[i] = ev[j]; ev[j] = t; }

    if (n == 0) { snprintf(out, len, "\xF0\x9F\x93\x85 Keine Termine."); return; }
    int show = n < 2 ? n : 2;
    size_t o = snprintf(out, len, "\xF0\x9F\x93\x85 N\xC3\xA4""chste Termine:");
    for (int i = 0; i < show && o < len; i++) {
        int yy = 0, mm = 0, dd = 0;
        sscanf(ev[i].date, "%d-%d-%d", &yy, &mm, &dd);
        o += snprintf(out + o, len - o, "\n%02d.%02d. %s", dd, mm, ev[i].label);
    }
}

// --- Boot-Meldung -----------------------------------------------------------
static void send_greeting(void)
{
    char w[160], net[200], msg[540];
    build_weather(w, sizeof(w));
    build_internet(net, sizeof(net));
    snprintf(msg, sizeof(msg), "\xF0\x9F\x94\x84 Wieder da!\n%s\n%s", w, net);
    telegram_send(msg);
}

// --- Befehle ----------------------------------------------------------------
// Datum formatieren ueber Zeiger (kein format-truncation-Check auf feste Groesse).
static void fmt_date(char *out, size_t n, int y, int m, int d)
{
    snprintf(out, n, "%04d-%02d-%02d", y, m, d);
}

// "neu termin <datum> <text>" -> Termin anlegen. Datum: JJJJ-MM-TT, TT.MM.JJJJ
// oder TT.MM. (laufendes Jahr). rest zeigt hinter das Wort "termin".
static void cmd_new_termin(const char *rest)
{
    while (*rest == ' ') rest++;
    char tok[24] = { 0 };
    int i = 0;
    while (rest[i] && rest[i] != ' ' && i < (int)sizeof(tok) - 1) { tok[i] = rest[i]; i++; }
    tok[i] = '\0';
    const char *title = rest + i;
    while (*title == ' ') title++;

    char date[16] = { 0 };
    int y = 0, m = 0, d = 0;
    if (sscanf(tok, "%4d-%2d-%2d", &y, &m, &d) == 3) {
        fmt_date(date, sizeof(date), y, m, d);
    } else if (sscanf(tok, "%2d.%2d.%4d", &d, &m, &y) == 3) {
        fmt_date(date, sizeof(date), y, m, d);
    } else if (sscanf(tok, "%2d.%2d", &d, &m) == 2) {
        time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
        fmt_date(date, sizeof(date), tm.tm_year + 1900, m, d);
    }
    // Plausibilitaet
    bool ok_date = (m >= 1 && m <= 12 && d >= 1 && d <= 31 && date[0]);

    char msg[240];
    if (!ok_date || title[0] == '\0' || !termine_add(date, "", title)) {
        snprintf(msg, sizeof(msg),
                 "Konnte Termin nicht anlegen. Format: neu termin JJJJ-MM-TT Text");
    } else {
        snprintf(msg, sizeof(msg), "\xE2\x9C\x85 Termin gespeichert: %02d.%02d. %s", d, m, title);
    }
    telegram_send(msg);
}

// Kommandoliste ausgeben.
static void send_help(void)
{
    char msg[400];
    snprintf(msg, sizeof(msg),
        "\xF0\x9F\xA4\x96 Kommandos (@%s ...):\n"
        "\xE2\x80\xA2 Wetter \xE2\x80\x93 aktuelle Spessartwetter-Werte\n"
        "\xE2\x80\xA2 internet \xE2\x80\x93 Internet-/Fritzbox-Daten\n"
        "\xE2\x80\xA2 termin \xE2\x80\x93 die n\xC3\xA4""chsten 2 Termine\n"
        "\xE2\x80\xA2 status \xE2\x80\x93 Uptime / Heap / WLAN\n"
        "\xE2\x80\xA2 neu termin JJJJ-MM-TT Text \xE2\x80\x93 Termin anlegen",
        s_botname[0] ? s_botname : "bot");
    telegram_send(msg);
}

// Prueft, ob der Bot direkt angesprochen wurde (@username), und beantwortet
// die Schluesselwoerter bzw. liefert die Kommandoliste.
static void handle_command(const char *text)
{
    if (s_botname[0] == '\0') return;
    char low[256];
    snprintf(low, sizeof(low), "%s", text);
    tolower_str(low);

    char mention[42];
    snprintf(mention, sizeof(mention), "@%s", s_botname);   // s_botname bereits klein
    if (!contains_ci(low, mention)) return;                 // nicht direkt angesprochen

    // "neu termin <datum> <text>" hat Vorrang (enthaelt sonst "termin")
    const char *nt = strstr(low, "neu termin");
    if (nt) {
        size_t pos = (size_t)(nt - low) + strlen("neu termin");
        cmd_new_termin(text + pos);   // Originaltext (Titel behaelt Gross/Klein)
        return;
    }

    char reply[560]; bool any = false;
    reply[0] = '\0'; size_t o = 0;
    if (contains_ci(low, "wetter")) {
        char b[160]; build_weather(b, sizeof(b));
        o += snprintf(reply + o, sizeof(reply) - o, "%s%s", any ? "\n" : "", b); any = true;
    }
    if (contains_ci(low, "internet")) {
        char b[200]; build_internet(b, sizeof(b));
        o += snprintf(reply + o, sizeof(reply) - o, "%s%s", any ? "\n" : "", b); any = true;
    }
    if (contains_ci(low, "termin")) {
        char b[200]; build_termine(b, sizeof(b));
        o += snprintf(reply + o, sizeof(reply) - o, "%s%s", any ? "\n" : "", b); any = true;
    }
    if (contains_ci(low, "status")) {
        char b[240]; sysstatus_text(b, sizeof(b));
        o += snprintf(reply + o, sizeof(reply) - o, "%s\xF0\x9F\x93\x8A %s", any ? "\n" : "", b); any = true;
    }
    if (!any) send_help();          // Erwaehnung ohne Kommando -> Hilfe
    else      telegram_send(reply);
}

// --- getMe (Bot-Username ermitteln) ----------------------------------------
static void fetch_botname(const char *token)
{
    char url[128];
    snprintf(url, sizeof(url), "%s%s/getMe", API, token);
    static char resp[512];
    if (http_get(url, resp, sizeof(resp)) <= 0) return;
    cJSON *root = cJSON_Parse(resp);
    if (root) {
        cJSON *res = cJSON_GetObjectItem(root, "result");
        cJSON *un = res ? cJSON_GetObjectItem(res, "username") : NULL;
        if (cJSON_IsString(un)) {
            snprintf(s_botname, sizeof(s_botname), "%s", un->valuestring);
            tolower_str(s_botname);
            ESP_LOGI(TAG, "Bot-Username: @%s", s_botname);
        }
        cJSON_Delete(root);
    }
}

// --- getUpdates -------------------------------------------------------------
// Liefert die hoechste verarbeitete update_id (oder -1). Bei process==false
// werden Nachrichten nur in den Verlauf geladen (kein Befehl ausgefuehrt).
static long long poll_updates(const char *token, const char *chat, bool process)
{
    char url[256];
    snprintf(url, sizeof(url),
             "%s%s/getUpdates?timeout=0&limit=10&offset=%lld", API, token, s_offset);

    char *buf = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(16384);
    if (!buf) return -1;

    int n = http_get(url, buf, 16384);
    if (n <= 0) { free(buf); return -1; }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    long long maxid = -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsArray(result)) {
        cJSON *upd;
        cJSON_ArrayForEach(upd, result) {
            cJSON *uid = cJSON_GetObjectItem(upd, "update_id");
            if (cJSON_IsNumber(uid)) {
                long long id = (long long)uid->valuedouble;
                if (id > maxid) maxid = id;
            }
            cJSON *msg = cJSON_GetObjectItem(upd, "message");
            if (!cJSON_IsObject(msg)) continue;

            // nur Nachrichten aus der konfigurierten Gruppe
            cJSON *ch = cJSON_GetObjectItem(msg, "chat");
            cJSON *chid = ch ? cJSON_GetObjectItem(ch, "id") : NULL;
            if (cJSON_IsNumber(chid)) {
                char cs[32]; snprintf(cs, sizeof(cs), "%.0f", chid->valuedouble);
                if (strcmp(cs, chat) != 0) continue;
            }

            cJSON *txt = cJSON_GetObjectItem(msg, "text");
            if (!cJSON_IsString(txt)) continue;
            cJSON *from = cJSON_GetObjectItem(msg, "from");
            cJSON *fn = from ? cJSON_GetObjectItem(from, "first_name") : NULL;
            const char *fname = cJSON_IsString(fn) ? fn->valuestring : "?";

            hist_add(fname, txt->valuestring);
            if (process) handle_command(txt->valuestring);
        }
    }
    cJSON_Delete(root);
    return maxid;
}

// --- DWD-Warnung posten -----------------------------------------------------
static void check_warning(void)
{
    dwd_data_t d; dwd_get(&d);
    if (!d.valid || d.count == 0 || d.headline[0] == '\0') return;
    if (strcmp(d.headline, s_last_warn) == 0) return;   // schon gepostet
    char msg[160];
    snprintf(msg, sizeof(msg), "\xE2\x9A\xA0 Wetterwarnung: %s", d.headline);
    if (telegram_send(msg)) snprintf(s_last_warn, sizeof(s_last_warn), "%s", d.headline);
}

// Amtliche Katastrophen-/Bevoelkerungsschutz-Warnung (BBK/NINA) posten.
static void check_nina(void)
{
    nina_data_t n; nina_get(&n);
    if (!n.valid || n.count == 0 || n.headline[0] == '\0') return;
    if (strcmp(n.headline, s_last_nina) == 0) return;   // schon gepostet
    char msg[200];
    snprintf(msg, sizeof(msg), "\xF0\x9F\x9A\xA8 Warnung (%s): %s",
             n.provider[0] ? n.provider : "BBK", n.headline);
    if (telegram_send(msg)) snprintf(s_last_nina, sizeof(s_last_nina), "%s", n.headline);
}

// --- Task -------------------------------------------------------------------
static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(6000));   // Netz/Poller Zeit zum Hochlaufen geben
    for (;;) {
        char token[64], chat[32];
        if (!get_token(token, sizeof(token)) || !get_chat(chat, sizeof(chat))) {
            vTaskDelay(pdMS_TO_TICKS(5000));   // noch nicht konfiguriert
            continue;
        }

        if (!s_primed) {
            fetch_botname(token);
            // Startabgleich: bestehende Updates konsumieren, ohne alte Befehle
            // erneut auszufuehren (nur in den Verlauf laden).
            long long last = poll_updates(token, chat, false);
            if (last >= 0) s_offset = last + 1;
            s_primed = true;
        }

        // Bot-Username evtl. beim ersten Versuch nicht geladen (Netz noch nicht
        // bereit / getMe fehlgeschlagen) -> weiter versuchen, sonst reagiert der
        // Bot NIE auf @-Erwaehnungen.
        if (s_botname[0] == '\0') fetch_botname(token);

        // Boot-Meldung, sobald Werte da sind (~12 s nach Start)
        if (!s_greeted && esp_timer_get_time() > 12 * 1000000LL) {
            send_greeting();
            s_greeted = true;
        }

        long long last = poll_updates(token, chat, true);
        if (last >= 0) s_offset = last + 1;

        check_warning();
        check_nina();
        // 10 s statt 4 s: deutlich seltenere TLS-Handshakes -> weniger PSRAM-
        // Bandbreiten-Spitzen, die die RGB-DMA stoeren (Display-Artefakte).
        // Bot-Kommandos werden dadurch max. ~10 s spaeter beantwortet.
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void telegram_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(poll_task, "telegram", 10240, NULL, 4, NULL);
}
