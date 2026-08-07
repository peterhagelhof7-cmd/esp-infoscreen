#include "fritzbox.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char *TAG = "fritzbox";
#define POLL_INTERVAL_MS 10000

static fritzbox_data_t s_data;
static SemaphoreHandle_t s_lock;

// --- SOAP-Aufruf ueber IGD/UPnP (Port 49000, ohne Auth) ----------------------
static bool soap_call(const char *host, const char *control, const char *service,
                      const char *action, char *resp, size_t resp_len)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:49000%s", host, control);

    char body[512];
    int blen = snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%s xmlns:u=\"%s\"></u:%s></s:Body></s:Envelope>",
        action, service, action);

    char soapaction[192];
    snprintf(soapaction, sizeof(soapaction), "%s#%s", service, action);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 4000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "Content-Type", "text/xml; charset=\"utf-8\"");
    esp_http_client_set_header(c, "SOAPAction", soapaction);

    bool ok = false;
    esp_err_t err = esp_http_client_open(c, blen);
    if (err == ESP_OK) {
        if (esp_http_client_write(c, body, blen) == blen) {
            esp_http_client_fetch_headers(c);
            int status = esp_http_client_get_status_code(c);
            int total = 0, r;
            while ((r = esp_http_client_read(c, resp + total, resp_len - 1 - total)) > 0) {
                total += r;
                if (total >= (int)resp_len - 1) break;
            }
            resp[total > 0 ? total : 0] = '\0';
            ok = (status == 200);
        }
        esp_http_client_close(c);
    }
    esp_http_client_cleanup(c);
    return ok;
}

// Wert eines XML-Tags <tag>...</tag> herausziehen.
static bool xml_val(const char *xml, const char *tag, char *out, size_t out_len)
{
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *s = strstr(xml, open);
    if (!s) return false;
    s += strlen(open);
    const char *e = strstr(s, close);
    if (!e) return false;
    size_t n = (size_t)(e - s);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

// Effektive Fritzbox-Adresse: Konfig "fb_host" wenn gesetzt, sonst Gateway.
static void effective_host(char *out, size_t len)
{
    if (config_get_str("fb_host", out, len) && out[0] != '\0') return;
    out[0] = '\0';
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.gw.addr != 0) {
        snprintf(out, len, IPSTR, IP2STR(&ip.gw));
    }
}

static void poll_once(void)
{
    char host[64];
    effective_host(host, sizeof(host));

    fritzbox_data_t d = { 0 };
    if (host[0] != '\0') {
        static char resp[2560];
        char val[64];

        if (soap_call(host, "/igdupnp/control/WANIPConn1",
                      "urn:schemas-upnp-org:service:WANIPConnection:1",
                      "GetExternalIPAddress", resp, sizeof(resp))
            && xml_val(resp, "NewExternalIPAddress", val, sizeof(val))) {
            strncpy(d.external_ip, val, sizeof(d.external_ip) - 1);
            d.reachable = true;
        }
        if (soap_call(host, "/igdupnp/control/WANCommonIFC1",
                      "urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1",
                      "GetCommonLinkProperties", resp, sizeof(resp))) {
            if (xml_val(resp, "NewLayer1DownstreamMaxBitRate", val, sizeof(val))) d.down_max_bps = strtoul(val, NULL, 10);
            if (xml_val(resp, "NewLayer1UpstreamMaxBitRate", val, sizeof(val)))   d.up_max_bps   = strtoul(val, NULL, 10);
            d.reachable = true;
        }
        if (soap_call(host, "/igdupnp/control/WANCommonIFC1",
                      "urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1",
                      "GetAddonInfos", resp, sizeof(resp))) {
            if (xml_val(resp, "NewByteReceiveRate", val, sizeof(val))) d.down_rate_Bps = strtoul(val, NULL, 10);
            if (xml_val(resp, "NewByteSendRate", val, sizeof(val)))    d.up_rate_Bps   = strtoul(val, NULL, 10);
            d.reachable = true;
        }
        if (!d.reachable) ESP_LOGW(TAG, "Fritzbox %s nicht erreichbar (UPnP aktiv?)", host);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = d;
    xSemaphoreGive(s_lock);
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void fritzbox_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "fritzbox", 8192, NULL, 3, NULL);
}

void fritzbox_get(fritzbox_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
