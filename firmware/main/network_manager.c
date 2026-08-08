#include "network_manager.h"
#include "config_store.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "network";
// Harter Zeit-Fallback: ist die STA nach dieser Zeit ab Boot nicht verbunden
// (SSID nicht gefunden / falsches Passwort / AP weg), wird der Einrichtungs-AP
// gestartet. ~1 Slideshow-Durchlauf (9 Slides x 10 s).
#define STA_FALLBACK_MS (90 * 1000)

static net_status_t s_status;
static SemaphoreHandle_t s_lock;
static int s_retry;
static bool s_ap_started;
static esp_timer_handle_t s_fb_timer;          // STA -> Einrichtungs-AP
static esp_timer_handle_t s_reconnect_timer;   // entzerrt die Verbindungsversuche

// --- gleitender RSSI-Mittelwert (letzte ~20 s, 1x/s abgetastet) ---
#define RSSI_WINDOW 20
static int8_t s_rssi_ring[RSSI_WINDOW];
static int s_rssi_count;
static int s_rssi_head;

static void rssi_sample_cb(void *arg)
{
    (void)arg;
    if (!s_status.connected) return;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return;
    s_rssi_ring[s_rssi_head] = ap.rssi;
    s_rssi_head = (s_rssi_head + 1) % RSSI_WINDOW;
    if (s_rssi_count < RSSI_WINDOW) s_rssi_count++;
}

int network_manager_get_avg_rssi(void)
{
    if (s_rssi_count == 0) return 0;
    int sum = 0;
    for (int i = 0; i < s_rssi_count; i++) sum += s_rssi_ring[i];
    return sum / s_rssi_count;
}

static void set_mode(net_mode_t m)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.mode = m;
    if (m != NET_MODE_STA_CONNECTED) { s_status.connected = false; s_status.ip[0] = '\0'; }
    xSemaphoreGive(s_lock);
}

static void start_installer_ap(void)
{
    ESP_LOGW(TAG, "Starte Einrichtungs-AP \"%s\" (%s)", NET_AP_SSID, NET_AP_IP);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));   // APSTA: AP fuer Konfig + STA zum Scannen
    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, NET_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(NET_AP_SSID);
    strncpy((char *)ap.ap.password, NET_AP_PASS, sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    s_ap_started = true;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.mode = NET_MODE_INSTALLER_AP;
    s_status.connected = false;
    strncpy(s_status.ip, NET_AP_IP, sizeof(s_status.ip));
    strncpy(s_status.ssid, NET_AP_SSID, sizeof(s_status.ssid));
    xSemaphoreGive(s_lock);
}

// Zeit-Fallback: nach STA_FALLBACK_MS ohne Verbindung -> Einrichtungs-AP.
static void fallback_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool connected = s_status.connected;
    net_mode_t mode = s_status.mode;
    xSemaphoreGive(s_lock);
    if (!connected && mode != NET_MODE_INSTALLER_AP) {
        ESP_LOGE(TAG, "WLAN nach %d s nicht verbunden -> Einrichtungs-AP", STA_FALLBACK_MS / 1000);
        start_installer_ap();
    }
}

static void start_fallback_timer(void)
{
    if (!s_fb_timer) return;
    esp_timer_stop(s_fb_timer);   // ggf. laufenden neu aufsetzen (harmlos, wenn keiner laeuft)
    esp_timer_start_once(s_fb_timer, (uint64_t)STA_FALLBACK_MS * 1000);
}

// Verbindungsversuch nach kurzer Pause (statt sofort). Verhindert einen
// Dauer-Scan-Loop (All-Channel-Scan belegt sonst die WLAN-Hardware dauerhaft ->
// manueller Netz-Scan im Web liefert nichts, mehr CPU/PSRAM-Last am Display).
static void reconnect_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}
static void schedule_reconnect(uint32_t delay_ms)
{
    if (!s_reconnect_timer) { esp_wifi_connect(); return; }
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_status.mode == NET_MODE_INSTALLER_AP) return;   // im AP-Modus nicht dagegenarbeiten
        s_retry++;
        ESP_LOGW(TAG, "STA getrennt (Versuch %d), erneut in 4 s ...", s_retry);
        schedule_reconnect(4000);   // entzerrt; Zeit-Fallback greift nach STA_FALLBACK_MS
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        if (s_fb_timer) esp_timer_stop(s_fb_timer);            // verbunden -> Fallback abbrechen
        if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);
        s_retry = 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.mode = NET_MODE_STA_CONNECTED;
        s_status.connected = true;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ev->ip_info.ip));
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "WLAN verbunden, IP %s", s_status.ip);
    }
}

static void connect_sta(void)
{
    char ssid[33] = { 0 }, pass[65] = { 0 };
    config_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    ESP_LOGI(TAG, "Verbinde mit WLAN \"%s\"", ssid);

    wifi_config_t sta = { 0 };
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    // KEINE feste BSSID -> verbindet mit der SSID, nicht mit einer bestimmten AP.
    // Alle Kanaele scannen und die STAERKSTE AP dieser SSID waehlen (statt der
    // erst gefundenen per Fast-Scan) -> zuverlaessig bei mehreren APs/Mesh und
    // nach Standortwechsel. Auth-Untergrenze WPA (akzeptiert WPA/WPA2/WPA3).
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.mode = NET_MODE_STA_CONNECTING;
    strncpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    xSemaphoreGive(s_lock);

    start_fallback_timer();   // nach STA_FALLBACK_MS ohne Verbindung -> Einrichtungs-AP
}

void network_manager_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = NET_MODE_BOOT;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // Timer: Fallback (STA -> Einrichtungs-AP) und entzerrter Reconnect
    const esp_timer_create_args_t fb = { .callback = fallback_cb, .name = "wifi_fallback" };
    esp_timer_create(&fb, &s_fb_timer);
    const esp_timer_create_args_t rc = { .callback = reconnect_cb, .name = "wifi_reconnect" };
    esp_timer_create(&rc, &s_reconnect_timer);

    // Hostname = Geraetename (Default esp-infoscreen)
    char host[32];
    config_get_str_def("dev_name", host, sizeof(host), "esp-infoscreen");
    if (sta_netif) esp_netif_set_hostname(sta_netif, host);

    // mDNS: Geraet als "<host>.local" im Browser erreichbar (kein IP-Suchen)
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(host);
        mdns_instance_name_set(host);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS aktiv: http://%s.local", host);
    } else {
        ESP_LOGW(TAG, "mDNS-Init fehlgeschlagen");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (config_has_wifi()) {
        connect_sta();
    } else {
        ESP_LOGW(TAG, "Kein WLAN konfiguriert -> Einrichtungs-AP");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        start_installer_ap();
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    // Modem-Sleep AUS: Power-Save (WIFI_PS_MIN_MODEM) laesst Scans im
    // verbundenen Zustand oft leer ausgehen und macht die Verbindung traeger.
    // Fuer ein dauerversorgtes Wanddisplay unnoetig -> volle Reaktionsfaehigkeit.
    esp_wifi_set_ps(WIFI_PS_NONE);

    // RSSI 1x/s abtasten (fuer den 20-s-Mittelwert auf der WLAN-Slide)
    const esp_timer_create_args_t ta = { .callback = rssi_sample_cb, .name = "rssi" };
    esp_timer_handle_t th;
    if (esp_timer_create(&ta, &th) == ESP_OK) esp_timer_start_periodic(th, 1000000);
}

void network_manager_get_status(net_status_t *out)
{
    // Defensiv: falls vor network_manager_init() abgefragt (Mutex noch NULL).
    if (!s_lock) { memset(out, 0, sizeof(*out)); out->mode = NET_MODE_BOOT; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
    // RSSI live nachziehen, wenn verbunden
    if (out->connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) out->rssi = ap.rssi;
    }
}

int network_manager_scan(net_ap_t *list, int max)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) { ESP_LOGW(TAG, "Scan-Start fehlgeschlagen: %s", esp_err_to_name(err)); return 0; }
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    ESP_LOGI(TAG, "Scan: %u Netze gefunden", (unsigned)num);
    if (num == 0) return 0;
    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    if (!recs) return 0;
    esp_wifi_scan_get_ap_records(&num, recs);

    int n = 0;
    for (int i = 0; i < num && n < max; i++) {
        if (recs[i].ssid[0] == '\0') continue;
        // Duplikate (gleiche SSID) ueberspringen
        bool dup = false;
        for (int j = 0; j < n; j++) if (strcmp(list[j].ssid, (char *)recs[i].ssid) == 0) { dup = true; break; }
        if (dup) continue;
        strncpy(list[n].ssid, (char *)recs[i].ssid, sizeof(list[n].ssid) - 1);
        list[n].ssid[sizeof(list[n].ssid) - 1] = '\0';
        list[n].rssi = recs[i].rssi;
        list[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }
    free(recs);
    return n;
}

void network_manager_apply_wifi(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "Neue WLAN-Daten gespeichert (SSID \"%s\") - Neustart", ssid);
    config_set_wifi(ssid, pass);
    vTaskDelay(pdMS_TO_TICKS(800));   // Zeit fuer die HTTP-Antwort
    esp_restart();
}
