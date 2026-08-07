#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NET_AP_SSID "installer"
#define NET_AP_PASS "installer"     // WPA2, mind. 8 Zeichen
#define NET_AP_IP   "192.168.4.1"

typedef enum {
    NET_MODE_BOOT,
    NET_MODE_STA_CONNECTING,
    NET_MODE_STA_CONNECTED,
    NET_MODE_INSTALLER_AP,
} net_mode_t;

typedef struct {
    net_mode_t mode;
    bool connected;
    char ssid[33];
    char ip[16];
    int8_t rssi;   // nur gueltig, wenn connected
} net_status_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} net_ap_t;

// Initialisiert WLAN: bei gespeicherten Zugangsdaten -> Verbindung als Station,
// sonst (oder bei dauerhaftem Fehlschlag) -> Einrichtungs-AP "installer".
void network_manager_init(void);

// Aktuellen Status (threadsicher) kopieren.
void network_manager_get_status(net_status_t *out);

// Netze scannen, bis zu max Eintraege; liefert Anzahl. Nur sinnvoll in
// STA/APSTA-Modus (im Installer-AP moeglich).
int network_manager_scan(net_ap_t *list, int max);

// Neue WLAN-Zugangsdaten speichern und Geraet neu starten (uebernimmt sie).
void network_manager_apply_wifi(const char *ssid, const char *pass);

// Gemittelter WLAN-Empfang (dBm) der letzten ~20 Sekunden. 0 = keine Daten
// (nicht verbunden). Wird intern jede Sekunde abgetastet.
int network_manager_get_avg_rssi(void);
