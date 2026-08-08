#pragma once
#include <stdbool.h>

typedef struct {
    bool valid;
    char label[14];   // "Morgen" / "Uebermorgen" / Wochentag
    int  tmin;        // erwartetes Minimum (Grad C)
    int  tmax;        // erwartetes Maximum (Grad C)
    char desc[32];    // Wetterbeschreibung (deutsch, Mittag)
    char icon[4];     // OWM-Icon-Code (z.B. "10d") fuer das Symbol
} owm_fc_day_t;

typedef struct {
    bool valid;        // aktuelle Werte gueltig
    bool has_key;      // API-Key konfiguriert?
    char desc[48];     // aktuelle Wetterbeschreibung
    char icon[4];      // OWM-Icon-Code (z.B. "01d")
    int  temp;         // Grad C
    int  feels;        // gefuehlt, Grad C
    int  humidity;     // %
    int  wind_kmh;     // km/h

    bool fc_valid;     // Vorhersage gueltig
    owm_fc_day_t fc[3];// morgen, uebermorgen, Tag danach
} owm_data_t;

// Startet den Hintergrund-Poller (OpenWeatherMap, Johannesberg).
// Aktuelle Werte alle 20 min (72/Tag), Vorhersage alle ~3 h (8/Tag) -> 80/Tag
// (unter dem 100/Tag-Limit). API-Key aus Konfig "owm_key".
void owm_init(void);

// Threadsichere Kopie der zuletzt abgefragten Werte.
void owm_get(owm_data_t *out);
