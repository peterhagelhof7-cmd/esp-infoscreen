#pragma once
#include <stdbool.h>
#include <stddef.h>

// Einfache Konfig-Persistenz auf NVS (Namespace "cfg"). Fuer WLAN-Zugangsdaten
// und spaeter API-Keys/Standort-IDs/Anzeige-Optionen.

void config_store_init(void);

// Generisch: String lesen/schreiben. Liefert bei get true, wenn vorhanden.
bool config_get_str(const char *key, char *out, size_t out_len);
void config_set_str(const char *key, const char *value);

// String mit Default lesen (schreibt out=def, wenn nicht vorhanden).
void config_get_str_def(const char *key, char *out, size_t out_len, const char *def);
int  config_get_int(const char *key, int def);
void config_set_int(const char *key, int value);

// WLAN-Komfort-Helfer.
bool config_has_wifi(void);
bool config_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void config_set_wifi(const char *ssid, const char *pass);

// Werksreset: alle Konfig-Schluessel loeschen.
void config_clear(void);

// Alle String-Konfig-Werte als JSON-Objekt exportieren (liefert Laenge, -1 Fehler).
int  config_export_json(char *buf, size_t buf_len);
// JSON-Objekt {key:value} importieren (jeder Wert als String gesetzt).
bool config_import_json(const char *json);
