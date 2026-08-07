#pragma once
#include <stdbool.h>
#include <stddef.h>

// Einfache Konfig-Persistenz auf NVS (Namespace "cfg"). Fuer WLAN-Zugangsdaten
// und spaeter API-Keys/Standort-IDs/Anzeige-Optionen.

void config_store_init(void);

// Generisch: String lesen/schreiben. Liefert bei get true, wenn vorhanden.
bool config_get_str(const char *key, char *out, size_t out_len);
void config_set_str(const char *key, const char *value);

// WLAN-Komfort-Helfer.
bool config_has_wifi(void);
bool config_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void config_set_wifi(const char *ssid, const char *pass);
