#include "web_server.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "web";

// --- kleine Helfer -----------------------------------------------------------
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URL-Decode in-place-artig nach out (%XX und '+'-Ersetzung).
static void url_decode(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_len; i++) {
        if (in[i] == '%' && hexval(in[i + 1]) >= 0 && hexval(in[i + 2]) >= 0) {
            out[o++] = (char)((hexval(in[i + 1]) << 4) | hexval(in[i + 2]));
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

// Feld "key=" aus einem x-www-form-urlencoded Body ziehen (roh, dann decodiert).
static bool form_field(const char *body, const char *key, char *out, size_t out_len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            const char *end = strchr(val, '&');
            size_t len = end ? (size_t)(end - val) : strlen(val);
            char raw[128];
            if (len >= sizeof(raw)) len = sizeof(raw) - 1;
            memcpy(raw, val, len);
            raw[len] = '\0';
            url_decode(raw, out, out_len);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

// HTML-escape fuer SSID-Anzeige (nur die noetigsten Zeichen).
static void html_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 6 < out_len; i++) {
        char c = in[i];
        if (c == '<')      { memcpy(out + o, "&lt;", 4);   o += 4; }
        else if (c == '>') { memcpy(out + o, "&gt;", 4);   o += 4; }
        else if (c == '&') { memcpy(out + o, "&amp;", 5);  o += 5; }
        else if (c == '"') { memcpy(out + o, "&quot;", 6); o += 6; }
        else out[o++] = c;
    }
    out[o] = '\0';
}

// --- Seite ------------------------------------------------------------------
static esp_err_t root_get(httpd_req_t *req)
{
    net_status_t st;
    network_manager_get_status(&st);

    // Netze scannen (bis 16)
    net_ap_t aps[16];
    int n = network_manager_scan(aps, 16);

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    static const char *head =
        "<!doctype html><html lang=de><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>esp-infoscreen Setup</title><style>"
        "body{font-family:system-ui,sans-serif;background:#101830;color:#eee;margin:0;padding:16px}"
        ".card{max-width:460px;margin:0 auto;background:#1b2440;border-radius:12px;padding:20px}"
        "h1{font-size:20px;margin:0 0 4px} .sub{color:#8ab4f8;font-size:13px;margin-bottom:16px}"
        "label{display:block;margin:12px 0 4px;font-size:14px} "
        "select,input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #33406a;background:#0d1428;color:#eee}"
        "button{margin-top:16px;width:100%;padding:12px;border:0;border-radius:8px;background:#3b6ef0;color:#fff;font-size:15px}"
        ".st{font-size:13px;color:#9aa4c0;margin-top:14px}</style></head><body><div class=card>"
        "<h1>esp-infoscreen</h1><div class=sub>WLAN-Einrichtung</div>"
        "<form method=post action=/save>"
        "<label>Netzwerk (SSID)</label><select name=ssid>";

    httpd_resp_sendstr_chunk(req, head);

    // SSID max 32 Byte, HTML-escaped bis ~6x -> grosszuegig dimensionieren,
    // damit der Compiler keine Truncation befuerchtet (esc 2x in row).
    char esc[208], row[512];
    for (int i = 0; i < n; i++) {
        html_escape(aps[i].ssid, esc, sizeof(esc));
        snprintf(row, sizeof(row), "<option value=\"%s\">%s (%d dBm)%s</option>",
                 esc, esc, aps[i].rssi, aps[i].secure ? " 🔒" : "");
        httpd_resp_sendstr_chunk(req, row);
    }
    if (n == 0) httpd_resp_sendstr_chunk(req, "<option value=''>(kein Netz gefunden)</option>");

    httpd_resp_sendstr_chunk(req,
        "</select>"
        "<label>Passwort</label><input type=password name=pass placeholder='WLAN-Passwort'>"
        "<button type=submit>Speichern &amp; Neustart</button></form>");

    char st_line[128];
    const char *modestr = (st.mode == NET_MODE_STA_CONNECTED) ? "verbunden"
                        : (st.mode == NET_MODE_INSTALLER_AP)  ? "Einrichtungs-AP"
                                                              : "verbinde...";
    snprintf(st_line, sizeof(st_line), "<div class=st>Status: %s &middot; IP: %s</div>",
             modestr, st.ip[0] ? st.ip : "-");
    httpd_resp_sendstr_chunk(req, st_line);
    httpd_resp_sendstr_chunk(req, "</div>");   // WLAN-Card schliessen

    // --- Anzeige-Card (180-Grad-Drehung) ---
    char rot[4];
    bool rotated = config_get_str("rot180", rot, sizeof(rot)) && rot[0] == '1';
    char disp_card[420];
    snprintf(disp_card, sizeof(disp_card),
        "<div class=card style='margin-top:16px'>"
        "<h1>Anzeige</h1><div class=sub>Ausrichtung (Neustart)</div>"
        "<form method=post action=/display>"
        "<label style='display:flex;align-items:center;gap:10px'>"
        "<input type=checkbox name=rot value=1 %s style='width:auto'> Anzeige um 180&deg; drehen</label>"
        "<button type=submit>Uebernehmen</button></form></div>",
        rotated ? "checked" : "");
    httpd_resp_sendstr_chunk(req, disp_card);

    // --- Firmware-Update-Card (OTA) ---
    httpd_resp_sendstr_chunk(req,
        "<div class=card style='margin-top:16px'>"
        "<h1>Firmware-Update</h1><div class=sub>.bin per OTA hochladen</div>"
        "<input type=file id=fw accept='.bin'>"
        "<button onclick='up()'>Hochladen &amp; Neustart</button>"
        "<div class=st id=ost></div></div>"
        "<script>"
        "function up(){var f=document.getElementById('fw').files[0];"
        "if(!f){alert('Bitte eine .bin-Datei waehlen');return;}"
        "var s=document.getElementById('ost');var x=new XMLHttpRequest();x.open('POST','/ota');"
        "x.upload.onprogress=function(e){if(e.lengthComputable)s.textContent='Hochladen... '+Math.round(e.loaded/e.total*100)+'%';};"
        "x.onload=function(){s.textContent=(x.status==200)?'OK - Geraet startet neu.':'Fehler: '+x.responseText;};"
        "x.onerror=function(){s.textContent='Upload-Fehler';};x.send(f);}"
        "</script>");

    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);   // Ende
    return ESP_OK;
}

static esp_err_t display_post(httpd_req_t *req)
{
    char body[128];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char tmp[8];
    bool rot = form_field(body, "rot", tmp, sizeof(tmp)) && tmp[0] == '1';
    config_set_str("rot180", rot ? "1" : "0");
    ESP_LOGI(TAG, "Anzeige-Drehung: %s (Neustart)", rot ? "180 Grad" : "0 Grad");

    // Die Drehung wird beim Init als HW-State des RGB-Panels gesetzt -> Neustart.
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><meta charset=utf-8><body style='font-family:sans-serif;background:#101830;color:#eee;padding:24px'>"
        "<h2>Gespeichert.</h2><p>Das Geraet startet neu und uebernimmt die Ausrichtung.</p></body>");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_post(httpd_req_t *req)
{
    if (!ota_manager_begin()) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "OTA-Start fehlgeschlagen");
        return ESP_OK;
    }
    char buf[2048];
    int remaining = req->content_len;
    while (remaining > 0) {
        int want = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int r = httpd_req_recv(req, buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            ota_manager_abort();
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "Empfang abgebrochen");
            return ESP_OK;
        }
        if (!ota_manager_write((const uint8_t *)buf, r)) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "Schreibfehler");
            return ESP_OK;
        }
        remaining -= r;
    }
    if (!ota_manager_finish()) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Image ungueltig");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "OK");
    ESP_LOGI(TAG, "OTA-Upload erfolgreich - Neustart");
    vTaskDelay(pdMS_TO_TICKS(1000));   // HTTP-Antwort rausschicken lassen
    esp_restart();
    return ESP_OK;   // nicht erreicht
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[512];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = httpd_req_recv(req, body, total);
    if (recvd <= 0) return ESP_FAIL;
    body[recvd] = '\0';

    char ssid[33] = { 0 }, pass[65] = { 0 };
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));

    if (ssid[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "SSID fehlt.");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><meta charset=utf-8><body style='font-family:sans-serif;background:#101830;color:#eee;padding:24px'>"
        "<h2>Gespeichert.</h2><p>Das Geraet startet neu und verbindet sich mit dem WLAN.</p></body>");

    ESP_LOGI(TAG, "WLAN-Konfiguration empfangen (SSID \"%s\")", ssid);
    network_manager_apply_wifi(ssid, pass);   // speichert + Neustart
    return ESP_OK;
}

void web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP-Server-Start fehlgeschlagen");
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t ota  = { .uri = "/ota", .method = HTTP_POST, .handler = ota_post };
    httpd_uri_t disp = { .uri = "/display", .method = HTTP_POST, .handler = display_post };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &ota);
    httpd_register_uri_handler(server, &disp);
    ESP_LOGI(TAG, "HTTP-Konfig-Server gestartet (Port 80)");
}
