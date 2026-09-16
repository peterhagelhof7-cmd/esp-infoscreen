#include "http_util.h"

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "http";

// Serialisiert alle HTTP(S)-Abrufe: nur EINE (TLS-)Verbindung gleichzeitig.
// Sonst allozieren mehrere parallele Poller je einen TLS-Kontext und sprengen
// den internen RAM -> mbedtls_ssl_setup schlaegt fehl (-0x008D).
static SemaphoreHandle_t s_gate;

// --- Download-Wächter -------------------------------------------------------
// Je Host merken wir uns den letzten Erfolg und den letzten Versuch. Ein Host
// gilt als gestoert, wenn er innerhalb der letzten Stunde versucht wurde, aber
// seit ueber einer Stunde nicht mehr erfolgreich war. httphealth_alert() (vom
// Telegram-Task gerufen) meldet solche Ausfaelle einmalig und die Erholung.
#define HH_MAX      16
#define HH_FAIL_S   3600           // >1h ohne Erfolg = Ausfall
typedef struct {
    char   host[64];
    time_t last_ok;
    time_t last_try;
    bool   alerted;
} hh_t;
static EXT_RAM_BSS_ATTR hh_t s_hh[HH_MAX];
static int    s_hh_n;
static SemaphoreHandle_t s_hh_lock;

void http_util_init(void)
{
    if (!s_gate)    s_gate    = xSemaphoreCreateMutex();
    if (!s_hh_lock) s_hh_lock = xSemaphoreCreateMutex();
}

static bool time_valid(time_t now)
{
    struct tm tm; localtime_r(&now, &tm);
    return tm.tm_year > 120;   // NTP gesetzt (Jahr > 2020)
}

// Host aus "scheme://host[:port]/..." extrahieren.
static void url_host(const char *url, char *out, size_t len)
{
    out[0] = '\0';
    const char *p = strstr(url, "://");
    if (!p) return;
    p += 3;
    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && i < len - 1) { out[i] = p[i]; i++; }
    out[i] = '\0';
}

static void hh_report(const char *url, bool ok)
{
    time_t now = time(NULL);
    if (!time_valid(now)) return;   // vor NTP keine sinnvolle Zeitrechnung
    char host[64]; url_host(url, host, sizeof(host));
    if (!host[0]) return;
    if (strstr(host, "telegram")) return;   // Transportkanal selbst nicht ueberwachen

    if (!s_hh_lock) return;
    xSemaphoreTake(s_hh_lock, portMAX_DELAY);
    hh_t *e = NULL;
    for (int i = 0; i < s_hh_n; i++) if (strcmp(s_hh[i].host, host) == 0) { e = &s_hh[i]; break; }
    if (!e && s_hh_n < HH_MAX) {
        e = &s_hh[s_hh_n++];
        snprintf(e->host, sizeof(e->host), "%s", host);
        e->last_ok = now;   // beim Erstkontakt als gesund annehmen (kein Boot-Fehlalarm)
        e->alerted = false;
    }
    if (e) {
        e->last_try = now;
        if (ok) { e->last_ok = now; e->alerted = false; }
    }
    xSemaphoreGive(s_hh_lock);
}

bool httphealth_alert(char *out, size_t len)
{
    time_t now = time(NULL);
    if (!time_valid(now) || !s_hh_lock) return false;

    size_t o = 0; int nfail = 0, nrec = 0;
    xSemaphoreTake(s_hh_lock, portMAX_DELAY);
    for (int i = 0; i < s_hh_n; i++) {
        hh_t *e = &s_hh[i];
        bool tried_recently = (now - e->last_try) < HH_FAIL_S;
        bool down = (now - e->last_ok) > HH_FAIL_S;
        if (down && tried_recently && !e->alerted) {
            if (o == 0) o += snprintf(out + o, len - o, "\xE2\x9A\xA0 Download-Problem (>1h):");
            o += snprintf(out + o, len - o, "\n- %s", e->host);
            e->alerted = true; nfail++;
        } else if (!down && e->alerted) {
            e->alerted = false; nrec++;   // still erholt; Sammelmeldung unten
        }
    }
    // Erholungen als eigene Zeile anhaengen (nur wenn Platz und keine reine Fehlerliste laeuft).
    if (nrec > 0) {
        if (o == 0) o += snprintf(out + o, len - o, "\xE2\x9C\x85 Downloads wieder ok");
        else        o += snprintf(out + o, len - o, "\n(%d Quelle(n) wieder ok)", nrec);
    }
    xSemaphoreGive(s_hh_lock);
    (void)nfail;
    return o > 0;
}

int http_get(const char *url, char *buf, size_t buf_len)
{
    if (s_gate) xSemaphoreTake(s_gate, portMAX_DELAY);

    int result = -1;
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS-Zertifikatspruefung
        .user_agent = "esp-infoscreen",
        // Groessere Puffer: lange URLs (z.B. Telegram sendMessage mit langem
        // Text) sprengen sonst den TX-Default (512 B) -> Request scheitert; und
        // manche Antwort-Header (Telegram) passen nicht in den RX-Default
        // ("Buffer length is small to fit all the headers").
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c) {
        esp_err_t err = esp_http_client_open(c, 0);
        if (err == ESP_OK) {
            esp_http_client_fetch_headers(c);
            int status = esp_http_client_get_status_code(c);
            int total = 0, r;
            while ((r = esp_http_client_read(c, buf + total, buf_len - 1 - total)) > 0) {
                total += r;
                if (total >= (int)buf_len - 1) break;
            }
            buf[total > 0 ? total : 0] = '\0';
            esp_http_client_close(c);
            if (status == 200) result = total;
            else ESP_LOGW(TAG, "GET %s -> HTTP %d", url, status);
        } else {
            ESP_LOGW(TAG, "GET %s -> open fehlgeschlagen: %s", url, esp_err_to_name(err));
        }
        esp_http_client_cleanup(c);
    }

    hh_report(url, result > 0);

    if (s_gate) xSemaphoreGive(s_gate);
    return result;
}
