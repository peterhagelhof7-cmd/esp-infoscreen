#pragma once
#include <stdbool.h>

typedef struct {
    bool valid;        // letzte Abfrage erfolgreich
    bool has_key;      // API-Key konfiguriert?
    char desc[48];     // Wetterbeschreibung (deutsch)
    int  temp;         // Grad C
    int  feels;        // gefuehlt, Grad C
    int  humidity;     // %
    int  wind_kmh;     // km/h
} owm_data_t;

// Startet den Hintergrund-Poller (OpenWeatherMap, Johannesberg).
// API-Key kommt aus der Konfig (Schluessel "owm_key"); ohne Key wird nicht
// abgefragt. Abfrage alle 20 min (= 72/Tag, unter dem 100/Tag-Limit).
void owm_init(void);

// Threadsichere Kopie der zuletzt abgefragten Werte.
void owm_get(owm_data_t *out);
