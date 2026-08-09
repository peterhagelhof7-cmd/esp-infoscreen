#include "web_server.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "config_store.h"
#include "termine.h"
#include "display.h"
#include "slides.h"
#include "telegram.h"
#include "muell.h"
#include "nina.h"
#include "sysstatus.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "cJSON.h"

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

// --- Einstellungsseite ------------------------------------------------------
static esp_err_t settings_get(httpd_req_t *req)
{
    net_status_t st;
    network_manager_get_status(&st);

    // Netze scannen (bis 16). static: HTTP-Handler laufen alle im EINEN Server-
    // Task (sequenziell) -> spart Stack (sonst Stack-Overflow der Seite).
    static net_ap_t aps[16];
    int n = network_manager_scan(aps, 16);
    ESP_LOGI(TAG, "Einstellungsseite: %d Netze im Dropdown", n);

    // Gemeinsamer Karten-Puffer (static): alle Karten werden nacheinander hier
    // aufgebaut und gesendet -> KEINE Aufsummierung auf dem Handler-Stack.
    // (HTTP-Handler laufen alle im EINEN Server-Task, sequenziell -> reentrant-sicher.)
    static char card[832];

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // Nicht cachen: sonst liefert der Browser beim "Netzwerke neu suchen" die
    // alte Seite (leeres/veraltetes Dropdown) statt neu zu scannen.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    static const char *head =
        "<!doctype html><html lang=de><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>esp-infoscreen Einstellungen</title><style>"
        "body{font-family:system-ui,sans-serif;background:#101830;color:#eee;margin:0;padding:16px}"
        ".card{max-width:460px;margin:0 auto;background:#1b2440;border-radius:12px;padding:20px}"
        "h1{font-size:20px;margin:0 0 4px} .sub{color:#8ab4f8;font-size:13px;margin-bottom:16px}"
        "label{display:block;margin:12px 0 4px;font-size:14px} "
        "select,input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #33406a;background:#0d1428;color:#eee}"
        "button{margin-top:16px;width:100%;padding:12px;border:0;border-radius:8px;background:#3b6ef0;color:#fff;font-size:15px}"
        ".navbtn{display:block;max-width:460px;margin:0 auto 12px;text-align:center;padding:10px;border-radius:8px;"
        "background:#28345c;color:#8ab4f8;text-decoration:none;font-size:14px}"
        ".st{font-size:13px;color:#9aa4c0;margin-top:14px}</style></head><body>"
        "<a class=navbtn href=/>&larr; Message Board</a><div class=card>"
        "<h1>esp-infoscreen</h1><div class=sub>WLAN-Einrichtung</div>"
        "<form method=post action=/save>"
        "<label>Netzwerk (SSID)</label><select name=ssid>";

    httpd_resp_sendstr_chunk(req, head);

    // SSID max 32 Byte, HTML-escaped bis ~6x -> grosszuegig dimensionieren
    // (esc taucht 2x in der Option auf). static: siehe card-Hinweis oben.
    static char esc[208];
    for (int i = 0; i < n; i++) {
        html_escape(aps[i].ssid, esc, sizeof(esc));
        snprintf(card, sizeof(card), "<option value=\"%s\">%s (%d dBm)%s</option>",
                 esc, esc, aps[i].rssi, aps[i].secure ? " 🔒" : "");
        httpd_resp_sendstr_chunk(req, card);
    }
    if (n == 0) httpd_resp_sendstr_chunk(req, "<option value=''>(kein Netz gefunden)</option>");

    httpd_resp_sendstr_chunk(req,
        "</select>"
        "<label>oder SSID manuell (falls nicht in der Liste)</label>"
        "<input type=text name=ssid_manual placeholder='SSID manuell eingeben (optional)' autocomplete=off>"
        "<label>Passwort</label><input type=password name=pass placeholder='WLAN-Passwort'>"
        "<button type=submit>Speichern &amp; Neustart</button></form>"
        "<a href=/settings style='display:block;text-align:center;color:#8ab4f8;margin-top:10px;font-size:13px'>Netzwerke neu suchen</a>");

    const char *modestr = (st.mode == NET_MODE_STA_CONNECTED) ? "verbunden"
                        : (st.mode == NET_MODE_INSTALLER_AP)  ? "Einrichtungs-AP"
                                                              : "verbinde...";
    snprintf(card, sizeof(card), "<div class=st>Status: %s &middot; IP: %s</div>",
             modestr, st.ip[0] ? st.ip : "-");
    httpd_resp_sendstr_chunk(req, card);
    httpd_resp_sendstr_chunk(req, "</div>");   // WLAN-Card schliessen

    // --- Geraet-Card (Name) ---
    char devname[32]; config_get_str_def("dev_name", devname, sizeof(devname), "esp-infoscreen");
    char dvesc[64]; html_escape(devname, dvesc, sizeof(dvesc));
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'><h1>Ger\xc3\xa4t</h1><div class=sub>Name (Neustart)</div>"
        "<form method=post action=/device><input type=text name=name maxlength=31 value=\"%s\">"
        "<button type=submit>Speichern</button></form></div>", dvesc);
    httpd_resp_sendstr_chunk(req, card);

    // --- Anzeige-Card: Helligkeit + Drehung + Slide-Auswahl + Intervall ---
    int bright = config_get_int("brightness", 100);
    int slide_sec = config_get_int("slide_sec", 10);
    char rot[4]; bool rotated = config_get_str("rot180", rot, sizeof(rot)) && rot[0] == '1';
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'><h1>Anzeige</h1>"
        "<form method=post action=/brightness>"
        "<label>Helligkeit: %d%%</label>"
        "<input type=range name=b min=5 max=100 value=%d oninput=\"this.previousElementSibling.textContent='Helligkeit: '+this.value+'%%'\">"
        "<button type=submit>\xc3\x9c" "bernehmen</button></form>"
        "<form method=post action=/display style='margin-top:8px'>"
        "<label style='display:flex;align-items:center;gap:10px'>"
        "<input type=checkbox name=rot value=1 %s style='width:auto'> Um 180&deg; drehen (Neustart)</label>"
        "<button type=submit>\xc3\x9c" "bernehmen</button></form>",
        bright, bright, rotated ? "checked" : "");
    httpd_resp_sendstr_chunk(req, card);

    httpd_resp_sendstr_chunk(req,
        "<form method=post action=/slides style='margin-top:8px'>"
        "<label>Angezeigte Slides</label>");
    for (int i = 0; i < slides_catalog_count(); i++) {
        char te[48]; html_escape(slides_catalog_title(i), te, sizeof(te));
        snprintf(card, sizeof(card),
            "<label style='display:flex;align-items:center;gap:10px;margin:4px 0'>"
            "<input type=checkbox name=s_%s value=1 %s style='width:auto'> %s</label>",
            slides_catalog_id(i), slides_catalog_enabled(i) ? "checked" : "", te);
        httpd_resp_sendstr_chunk(req, card);
    }
    snprintf(card, sizeof(card),
        "<label>Wechsel alle (Sekunden)</label>"
        "<input type=number name=sec min=3 max=120 value=%d>"
        "<button type=submit>Speichern &amp; Neustart</button></form></div>", slide_sec);
    httpd_resp_sendstr_chunk(req, card);

    // --- Termine-Card (anlegen / loeschen) ---
    httpd_resp_sendstr_chunk(req,
        "<div class=card style='margin-top:16px'><h1>Termine</h1>"
        "<div class=sub>n\xc3\xa4" "chste Termine (erscheinen im Kalender)</div>");
    {
        static termine_entry_t te[50];   // static: siehe aps-Hinweis oben (Stack)
        int tn = termine_get_all(te, 50);
        for (int i = 0; i < tn; i++) {
            char de[128];
            html_escape(te[i].title, de, sizeof(de));
            snprintf(card, sizeof(card),
                "<div style='display:flex;justify-content:space-between;align-items:center;margin:6px 0'>"
                "<span>%s %s %s</span>"
                "<form method=post action=/tdel style='margin:0'>"
                "<input type=hidden name=i value=%d>"
                "<button type=submit style='width:auto;padding:4px 10px;background:#8a3a3a'>x</button></form></div>",
                te[i].date, te[i].time[0] ? te[i].time : "", de, i);
            httpd_resp_sendstr_chunk(req, card);
        }
        if (tn == 0) httpd_resp_sendstr_chunk(req, "<div class=st>keine Termine</div>");
    }
    httpd_resp_sendstr_chunk(req,
        "<form method=post action=/tadd style='margin-top:12px'>"
        "<label>Datum</label><input type=date name=date required>"
        "<label>Uhrzeit (optional)</label><input type=time name=time>"
        "<label>Titel</label><input type=text name=title maxlength=39 required>"
        "<button type=submit>Termin hinzuf\xc3\xbcgen</button></form></div>");

    // --- Fritzbox-Card (Adresse, leer = Gateway) ---
    char fbhost[64] = { 0 };
    config_get_str("fb_host", fbhost, sizeof(fbhost));
    char fbesc[80];
    html_escape(fbhost, fbesc, sizeof(fbesc));
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'>"
        "<h1>Fritzbox</h1><div class=sub>Adresse (leer = Gateway)</div>"
        "<form method=post action=/fritzbox>"
        "<input type=text name=host value=\"%s\" placeholder='z.B. 192.168.178.1 oder fritz.box'>"
        "<button type=submit>Speichern</button></form>"
        "<div class=st>Nutzt UPnP/IGD (Port 49000). In der Fritzbox muss "
        "\"Statusinformationen &uuml;ber UPnP &uuml;bertragen\" aktiviert sein.</div></div>",
        fbesc);
    httpd_resp_sendstr_chunk(req, card);

    // --- Muell-Card (jumomind Ortsteil-ID) ---
    char muellid[16]; config_get_str_def("muell_id", muellid, sizeof(muellid), "44886");
    char muellid_esc[24]; html_escape(muellid, muellid_esc, sizeof(muellid_esc));
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'>"
        "<h1>M\xc3\xbcll</h1><div class=sub>Ortsteil-ID (jumomind / MyM\xc3\xbcll)</div>"
        "<form method=post action=/muell>"
        "<input type=text name=id value=\"%s\" placeholder='z.B. 44886'>"
        "<button type=submit>Speichern</button></form>"
        "<div class=st>Standard 44886 = Johannesberg-Oberafferbach. Andere: Johannesberg 29018, "
        "Breunsberg 7639, R\xc3\xbc" "ckersbach 53323, Steinbach 59146, Sternberg 59533.</div></div>",
        muellid_esc);
    httpd_resp_sendstr_chunk(req, card);

    // --- Warnungen-Card (BBK/NINA Regionalschluessel) ---
    char ars[16]; config_get_str_def("nina_ars", ars, sizeof(ars), "096710000000");
    char ars_esc[24]; html_escape(ars, ars_esc, sizeof(ars_esc));
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'>"
        "<h1>Warnungen</h1><div class=sub>Katastrophenschutz (BBK/NINA)</div>"
        "<form method=post action=/nina>"
        "<label>Warnregion (Amtlicher Regionalschl\xc3\xbcssel, 12-stellig)</label>"
        "<input type=text name=ars value=\"%s\" placeholder='096710000000'>"
        "<button type=submit>Speichern</button></form>"
        "<div class=st>Standard 096710000000 = Landkreis Aschaffenburg. Deckt MoWaS, "
        "Hochwasser, KATWARN u. DWD ab. Kreis-Schl\xc3\xbcssel + \"0000000\".</div></div>",
        ars_esc);
    httpd_resp_sendstr_chunk(req, card);

    // --- OpenWeatherMap-Card (API-Key + Standort) ---
    char owmkey[48] = { 0 };
    bool has_key = config_get_str("owm_key", owmkey, sizeof(owmkey)) && owmkey[0];
    char owmloc[64] = { 0 };
    config_get_str("owm_loc", owmloc, sizeof(owmloc));
    char owmloc_esc[80]; html_escape(owmloc, owmloc_esc, sizeof(owmloc_esc));
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'>"
        "<h1>Wetter</h1><div class=sub>OpenWeatherMap</div>"
        "<form method=post action=/owm>"
        "<label>API-Key</label>"
        "<input type=text name=key placeholder='%s' autocomplete=off>"
        "<label>Standort (leer = Johannesberg)</label>"
        "<input type=text name=loc value=\"%s\" placeholder='z.B. Aschaffenburg,DE'>"
        "<button type=submit>Speichern</button></form>"
        "<div class=st>Kostenloser Key von openweathermap.org. %s</div></div>",
        has_key ? "gesetzt - zum \xc3\x84ndern neuen Key eingeben" : "noch nicht gesetzt",
        owmloc_esc,
        has_key ? "Key aktuell konfiguriert." : "");
    httpd_resp_sendstr_chunk(req, card);

    // --- Telegram-Card (Bot-Token + Gruppen-ID) ---
    char tgchat[32] = { 0 };
    config_get_str("tg_chat", tgchat, sizeof(tgchat));
    char tgchat_esc[48]; html_escape(tgchat, tgchat_esc, sizeof(tgchat_esc));
    char tgtoken[64] = { 0 };
    bool has_tok = config_get_str("tg_token", tgtoken, sizeof(tgtoken)) && tgtoken[0];
    snprintf(card, sizeof(card),
        "<div class=card style='margin-top:16px'>"
        "<h1>Telegram</h1><div class=sub>Message Board / Bot</div>"
        "<form method=post action=/tg/config>"
        "<label>Bot-Token</label>"
        "<input type=text name=token placeholder='%s' autocomplete=off>"
        "<label>Gruppen-Chat-ID</label>"
        "<input type=text name=chat value=\"%s\" placeholder='z.B. -1001234567890'>"
        "<button type=submit>Speichern</button></form>"
        "<div class=st>Token vom @BotFather. Chat-ID der Gruppe (negativ). "
        "Der Bot muss Mitglied der Gruppe sein. %s</div></div>",
        has_tok ? "gesetzt - zum \xc3\x84ndern neuen Token eingeben" : "noch nicht gesetzt",
        tgchat_esc,
        has_tok ? "Token aktuell konfiguriert." : "");
    httpd_resp_sendstr_chunk(req, card);

    // --- Firmware-Update-Card (OTA) ---
    httpd_resp_sendstr_chunk(req,
        "<div class=card style='margin-top:16px'>"
        "<h1>Firmware-Update</h1><div class=sub>.bin per OTA hochladen</div>"
        "<input type=file id=fw accept='.bin'>"
        "<button onclick='up()'>Hochladen &amp; Neustart</button>"
        "<div class=st id=ost></div></div>"
        "<script>"
        "function up(){var f=document.getElementById('fw').files[0];"
        "if(!f){alert('Bitte eine .bin-Datei w\xc3\xa4hlen');return;}"
        "var s=document.getElementById('ost');var x=new XMLHttpRequest();x.open('POST','/ota');"
        "x.upload.onprogress=function(e){if(e.lengthComputable)s.textContent='Hochladen... '+Math.round(e.loaded/e.total*100)+'%';};"
        "x.onload=function(){s.textContent=(x.status==200)?'OK - Ger\xc3\xa4t startet neu.':'Fehler: '+x.responseText;};"
        "x.onerror=function(){s.textContent='Upload-Fehler';};x.send(f);}"
        "</script>");

    // --- Status-Card (Uptime, Heap, PSRAM, WLAN, Firmware) ---
    {
        char status[240]; sysstatus_text(status, sizeof(status));
        snprintf(card, sizeof(card),
            "<div class=card style='margin-top:16px'><h1>Status</h1>"
            "<pre style='white-space:pre-wrap;margin:0;font-size:14px;color:#b0b8d0'>%s</pre></div>",
            status);
        httpd_resp_sendstr_chunk(req, card);
    }

    // --- System-Card (Einstellungen laden/speichern, Werksreset, Neustart) ---
    httpd_resp_sendstr_chunk(req,
        "<div class=card style='margin-top:16px'><h1>System</h1>"
        "<a href=/config/download style='display:block;text-align:center;padding:12px;margin-top:8px;"
        "border-radius:8px;background:#3b6ef0;color:#fff;text-decoration:none'>Einstellungen herunterladen</a>"
        "<label>Einstellungen laden (.json)</label>"
        "<input type=file id=cf accept='.json,application/json'>"
        "<button onclick='cu()'>Hochladen &amp; Neustart</button>"
        "<div class=st id=cst></div>"
        "<form method=post action=/reboot style='margin-top:16px'>"
        "<button type=submit style='background:#5a6478'>Neustart</button></form>"
        "<form method=post action=/factory style='margin-top:16px'>"
        "<label>Werksreset - zur Best\xc3\xa4tigung Ger\xc3\xa4tenamen eingeben</label>"
        "<input type=text name=confirm placeholder='Ger\xc3\xa4tename' autocomplete=off>"
        "<button type=submit style='background:#a33a3a'>Werksreset &amp; Neustart</button></form></div>"
        "<script>"
        "function cu(){var f=document.getElementById('cf').files[0];"
        "if(!f){alert('Bitte eine .json-Datei w\xc3\xa4hlen');return;}"
        "var s=document.getElementById('cst');var x=new XMLHttpRequest();x.open('POST','/config/upload');"
        "x.onload=function(){s.textContent=(x.status==200)?'OK - Ger\xc3\xa4t startet neu.':'Fehler: '+x.responseText;};"
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
        "<h2>Gespeichert.</h2><p>Das Ger\xc3\xa4t startet neu und \xc3\xbc" "bernimmt die Ausrichtung.</p></body>");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

static esp_err_t redirect_settings(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/settings");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t tadd_post(httpd_req_t *req)
{
    char body[256];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char date[16] = { 0 }, time[8] = { 0 }, title[48] = { 0 };
    form_field(body, "date", date, sizeof(date));
    form_field(body, "time", time, sizeof(time));
    form_field(body, "title", title, sizeof(title));
    termine_add(date, time, title);
    return redirect_settings(req);
}

static esp_err_t tdel_post(httpd_req_t *req)
{
    char body[64];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char idx[8] = { 0 };
    if (form_field(body, "i", idx, sizeof(idx))) termine_delete(atoi(idx));
    return redirect_settings(req);
}

static esp_err_t owm_post(httpd_req_t *req)
{
    char body[128];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char key[48] = { 0 }, loc[64] = { 0 };
    if (form_field(body, "key", key, sizeof(key)) && key[0]) {
        config_set_str("owm_key", key);   // nur bei nicht-leerer Eingabe ueberschreiben
        ESP_LOGI(TAG, "OpenWeatherMap-Key gesetzt");
    }
    form_field(body, "loc", loc, sizeof(loc));
    config_set_str("owm_loc", loc);       // leer = Standort Johannesberg
    return redirect_settings(req);
}

static esp_err_t device_post(httpd_req_t *req)
{
    char body[128];
    int t = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int r = t > 0 ? httpd_req_recv(req, body, t) : 0;
    if (r < 0) return ESP_FAIL;
    body[r > 0 ? r : 0] = '\0';
    char name[32] = { 0 };
    if (form_field(body, "name", name, sizeof(name)) && name[0]) config_set_str("dev_name", name);
    // Hostname wird beim Start gesetzt -> Neustart
    httpd_resp_sendstr(req, "Gespeichert. Neustart ...");
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
    return ESP_OK;
}

static esp_err_t brightness_post(httpd_req_t *req)
{
    char body[64];
    int t = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int r = t > 0 ? httpd_req_recv(req, body, t) : 0;
    if (r < 0) return ESP_FAIL;
    body[r > 0 ? r : 0] = '\0';
    char b[8] = { 0 };
    if (form_field(body, "b", b, sizeof(b))) {
        int pct = atoi(b);
        if (pct < 5) pct = 5;
        if (pct > 100) pct = 100;
        config_set_int("brightness", pct);
        display_set_brightness(pct);   // sofort anwenden
    }
    return redirect_settings(req);
}

static esp_err_t slides_post(httpd_req_t *req)
{
    char body[512];
    int t = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int r = t > 0 ? httpd_req_recv(req, body, t) : 0;
    if (r < 0) return ESP_FAIL;
    body[r > 0 ? r : 0] = '\0';

    // Fuer jede Slide: aktiviert, wenn ihr Checkbox-Feld "s_<id>" vorhanden ist.
    char tmp[8];
    for (int i = 0; i < slides_catalog_count(); i++) {
        char field[16]; snprintf(field, sizeof(field), "s_%s", slides_catalog_id(i));
        bool on = form_field(body, field, tmp, sizeof(tmp));
        char key[16]; snprintf(key, sizeof(key), "sl_%s", slides_catalog_id(i));
        config_set_str(key, on ? "1" : "0");
    }
    char sec[8] = { 0 };
    if (form_field(body, "sec", sec, sizeof(sec)) && sec[0]) config_set_int("slide_sec", atoi(sec));

    httpd_resp_sendstr(req, "Gespeichert. Neustart ...");
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
    return ESP_OK;
}

static esp_err_t configdl_get(httpd_req_t *req)
{
    static EXT_RAM_BSS_ATTR char buf[8192];   // PSRAM: Termine-Liste kann gross sein
    int n = config_export_json(buf, sizeof(buf));
    if (n < 0) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Export fehlgeschlagen"); return ESP_OK; }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"esp-infoscreen-config.json\"");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t configul_post(httpd_req_t *req)
{
    static EXT_RAM_BSS_ATTR char buf[8192];   // PSRAM
    int total = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;
    int recvd = httpd_req_recv(req, buf, total);
    if (recvd <= 0) return ESP_FAIL;
    buf[recvd] = '\0';
    bool ok = config_import_json(buf);
    if (!ok) { httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, "Ung\xc3\xbcltiges JSON"); return ESP_OK; }
    httpd_resp_sendstr(req, "OK");
    ESP_LOGI(TAG, "Einstellungen importiert - Neustart");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Neustart ...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t factory_post(httpd_req_t *req)
{
    char body[128];
    int t = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int r = t > 0 ? httpd_req_recv(req, body, t) : 0;
    if (r < 0) return ESP_FAIL;
    body[r > 0 ? r : 0] = '\0';

    char confirm[32] = { 0 }, name[32];
    form_field(body, "confirm", confirm, sizeof(confirm));
    config_get_str_def("dev_name", name, sizeof(name), "esp-infoscreen");
    if (strcmp(confirm, name) != 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Ger\xc3\xa4tename stimmt nicht - Werksreset abgebrochen.");
        return ESP_OK;
    }
    config_clear();
    httpd_resp_sendstr(req, "Werksreset. Neustart ...");
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
    return ESP_OK;
}

static esp_err_t muell_post(httpd_req_t *req)
{
    char body[64];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char id[16] = { 0 };
    form_field(body, "id", id, sizeof(id));
    config_set_str("muell_id", id[0] ? id : "44886");
    muell_refresh();   // sofort mit neuer ID neu abrufen (kein Neustart noetig)
    ESP_LOGI(TAG, "Muell-Ortsteil-ID gesetzt: %s", id[0] ? id : "44886");
    return redirect_settings(req);
}

static esp_err_t nina_post(httpd_req_t *req)
{
    char body[64];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char ars[16] = { 0 };
    form_field(body, "ars", ars, sizeof(ars));
    config_set_str("nina_ars", ars[0] ? ars : "096710000000");
    nina_refresh();   // sofort mit neuer Region neu abrufen
    ESP_LOGI(TAG, "NINA-Warnregion (ARS) gesetzt: %s", ars[0] ? ars : "096710000000");
    return redirect_settings(req);
}

static esp_err_t fritzbox_post(httpd_req_t *req)
{
    char body[256];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char host[64] = { 0 };
    form_field(body, "host", host, sizeof(host));
    config_set_str("fb_host", host);   // leer = Gateway; wird beim naechsten Poll uebernommen
    ESP_LOGI(TAG, "Fritzbox-Adresse gesetzt: '%s'", host[0] ? host : "(Gateway)");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
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
        httpd_resp_sendstr(req, "Image ung\xc3\xbcltig");
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

    char ssid[33] = { 0 }, ssid_manual[33] = { 0 }, pass[65] = { 0 };
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "ssid_manual", ssid_manual, sizeof(ssid_manual));
    form_field(body, "pass", pass, sizeof(pass));

    // Manuell eingetippte SSID hat Vorrang (falls das Netz nicht im Scan war).
    const char *use_ssid = ssid_manual[0] ? ssid_manual : ssid;
    if (use_ssid[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "SSID fehlt (Netz w\xc3\xa4hlen oder manuell eingeben).");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><meta charset=utf-8><body style='font-family:sans-serif;background:#101830;color:#eee;padding:24px'>"
        "<h2>Gespeichert.</h2><p>Das Ger\xc3\xa4t startet neu und verbindet sich mit dem WLAN.</p></body>");

    ESP_LOGI(TAG, "WLAN-Konfiguration empfangen (SSID \"%s\")", use_ssid);
    network_manager_apply_wifi(use_ssid, pass);   // speichert + Neustart
    return ESP_OK;
}

// --- Message Board (Startseite) ---------------------------------------------
static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><html lang=de><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Message Board</title><style>"
        "body{font-family:system-ui,sans-serif;background:#101830;color:#eee;margin:0;padding:16px}"
        ".card{max-width:640px;margin:0 auto;background:#1b2440;border-radius:12px;padding:16px}"
        ".top{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}"
        "h1{font-size:20px;margin:0}"
        ".cog{padding:8px 14px;border-radius:8px;background:#28345c;color:#8ab4f8;text-decoration:none;font-size:14px}"
        "#chat{height:60vh;overflow-y:auto;background:#0d1428;border-radius:8px;padding:10px;font-size:15px;line-height:1.5}"
        ".msg{margin:4px 0} .msg b{color:#8ab4f8}"
        "form{display:flex;gap:8px;margin-top:12px}"
        "input{flex:1;padding:10px;border-radius:8px;border:1px solid #33406a;background:#0d1428;color:#eee}"
        "button{padding:10px 18px;border:0;border-radius:8px;background:#3b6ef0;color:#fff;font-size:15px}"
        "</style></head><body><div class=card>"
        "<div class=top><h1>Message Board</h1><a class=cog href=/settings>&#9881; Einstellungen</a></div>"
        "<div id=chat></div>"
        "<form onsubmit='return send(event)'>"
        "<input id=msg maxlength=400 placeholder='Nachricht an die Gruppe ...' autocomplete=off>"
        "<button type=submit>Senden</button></form></div>"
        "<script>"
        "async function load(){try{let r=await fetch('/tg/history.json');let a=await r.json();"
        "let c=document.getElementById('chat');let atb=c.scrollTop+c.clientHeight>=c.scrollHeight-20;"
        "c.innerHTML='';for(const m of a){let d=document.createElement('div');d.className='msg';"
        "let b=document.createElement('b');b.textContent=m.from+': ';d.appendChild(b);"
        "d.appendChild(document.createTextNode(m.text));c.appendChild(d);}"
        "if(atb)c.scrollTop=c.scrollHeight;}catch(e){}}"
        "async function send(e){e.preventDefault();let i=document.getElementById('msg');"
        "let t=i.value.trim();if(!t)return false;"
        "await fetch('/tg/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'text='+encodeURIComponent(t)});i.value='';setTimeout(load,400);return false;}"
        "load();setInterval(load,5000);"
        "</script></body></html>");
    return ESP_OK;
}

static esp_err_t tg_history_get(httpd_req_t *req)
{
    tg_msg_t msgs[TG_MAX_MSGS];
    int n = telegram_get_history(msgs, TG_MAX_MSGS);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "from", msgs[i].from);
        cJSON_AddStringToObject(o, "text", msgs[i].text);
        cJSON_AddItemToArray(arr, o);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "[]");
    if (json) cJSON_free(json);
    return ESP_OK;
}

static esp_err_t tg_send_post(httpd_req_t *req)
{
    char body[600];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char text[420] = { 0 };
    if (!form_field(body, "text", text, sizeof(text)) || text[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "leer");
        return ESP_OK;
    }
    if (!telegram_send(text)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Telegram nicht konfiguriert oder Fehler");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t tg_config_post(httpd_req_t *req)
{
    char body[256];
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int recvd = total > 0 ? httpd_req_recv(req, body, total) : 0;
    if (recvd < 0) return ESP_FAIL;
    body[recvd > 0 ? recvd : 0] = '\0';

    char token[64] = { 0 }, chat[32] = { 0 };
    if (form_field(body, "token", token, sizeof(token)) && token[0])
        config_set_str("tg_token", token);   // nur bei nicht-leerer Eingabe ueberschreiben
    form_field(body, "chat", chat, sizeof(chat));
    config_set_str("tg_chat", chat);
    ESP_LOGI(TAG, "Telegram-Konfig gespeichert (Chat '%s')", chat);
    return redirect_settings(req);
}

void web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    // 8192 reicht: die grossen Settings-Puffer (aps/te) sind static, nicht auf
    // dem Handler-Stack. Kleinerer Block passt auch bei fragmentiertem Heap.
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 28;
    httpd_handle_t server = NULL;
    // httpd_start kann beim Boot transient scheitern (interner RAM durch WiFi-Init
    // gerade knapp / TCP-IP noch nicht bereit) -> mehrfach versuchen, Fehler loggen.
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 10; attempt++) {
        err = httpd_start(&server, &cfg);
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "httpd_start Versuch %d/%d: %s", attempt, 10, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP-Server-Start endgueltig fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t sett = { .uri = "/settings", .method = HTTP_GET, .handler = settings_get };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t ota  = { .uri = "/ota", .method = HTTP_POST, .handler = ota_post };
    httpd_uri_t disp = { .uri = "/display", .method = HTTP_POST, .handler = display_post };
    httpd_uri_t fb   = { .uri = "/fritzbox", .method = HTTP_POST, .handler = fritzbox_post };
    httpd_uri_t mue  = { .uri = "/muell", .method = HTTP_POST, .handler = muell_post };
    httpd_uri_t nna  = { .uri = "/nina", .method = HTTP_POST, .handler = nina_post };
    httpd_uri_t tadd = { .uri = "/tadd", .method = HTTP_POST, .handler = tadd_post };
    httpd_uri_t tdel = { .uri = "/tdel", .method = HTTP_POST, .handler = tdel_post };
    httpd_uri_t owm  = { .uri = "/owm", .method = HTTP_POST, .handler = owm_post };
    httpd_uri_t dev  = { .uri = "/device", .method = HTTP_POST, .handler = device_post };
    httpd_uri_t brg  = { .uri = "/brightness", .method = HTTP_POST, .handler = brightness_post };
    httpd_uri_t sld  = { .uri = "/slides", .method = HTTP_POST, .handler = slides_post };
    httpd_uri_t cdl  = { .uri = "/config/download", .method = HTTP_GET, .handler = configdl_get };
    httpd_uri_t cul  = { .uri = "/config/upload", .method = HTTP_POST, .handler = configul_post };
    httpd_uri_t rbt  = { .uri = "/reboot", .method = HTTP_POST, .handler = reboot_post };
    httpd_uri_t fac  = { .uri = "/factory", .method = HTTP_POST, .handler = factory_post };
    httpd_uri_t tgh  = { .uri = "/tg/history.json", .method = HTTP_GET, .handler = tg_history_get };
    httpd_uri_t tgs  = { .uri = "/tg/send", .method = HTTP_POST, .handler = tg_send_post };
    httpd_uri_t tgc  = { .uri = "/tg/config", .method = HTTP_POST, .handler = tg_config_post };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &sett);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &ota);
    httpd_register_uri_handler(server, &disp);
    httpd_register_uri_handler(server, &fb);
    httpd_register_uri_handler(server, &mue);
    httpd_register_uri_handler(server, &nna);
    httpd_register_uri_handler(server, &tadd);
    httpd_register_uri_handler(server, &tdel);
    httpd_register_uri_handler(server, &owm);
    httpd_register_uri_handler(server, &dev);
    httpd_register_uri_handler(server, &brg);
    httpd_register_uri_handler(server, &sld);
    httpd_register_uri_handler(server, &cdl);
    httpd_register_uri_handler(server, &cul);
    httpd_register_uri_handler(server, &rbt);
    httpd_register_uri_handler(server, &fac);
    httpd_register_uri_handler(server, &tgh);
    httpd_register_uri_handler(server, &tgs);
    httpd_register_uri_handler(server, &tgc);
    ESP_LOGI(TAG, "HTTP-Konfig-Server gestartet (Port 80)");
}
