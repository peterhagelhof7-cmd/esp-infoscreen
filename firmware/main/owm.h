#pragma once
#include <stdbool.h>

typedef struct {
    bool valid;
    char label[14];   // "Morgen" / "Uebermorgen" / Wochentag
    int  tmin;        // erwartetes Minimum (Grad C)
    int  tmax;        // erwartetes Maximum (Grad C)
    char desc[32];    // Wetterbeschreibung (deutsch, Mittag)
    char icon[4];     // OWM-Icon-Code (z.B. "10d") fuer das Symbol
    // Tages-Aggregat ueber alle 3-h-Slots (fuer die Unwetter-Hinweise im
    // 6-Uhr-Bericht; bei "heute" nur die noch kommenden Slots)
    float rain_mm;    // Summe Regen (rain.3h)
    float snow_mm;    // Summe Schnee (snow.3h)
    int   gust_kmh;   // staerkste Boe
    int   pop;        // max. Niederschlagswahrscheinlichkeit (%)
    bool  thunder;    // mind. ein Slot mit Gewitter (Wetter-ID 2xx)
    bool  freezing;   // gefrierender Regen/Niesel (ID 511) oder Schneeregen (611-616)
    bool  fog;        // Nebel (ID 741) oder Sicht < 1 km in mind. einem Slot
    int   tmin_morning; // Tiefstwert der Slots bis ~8 Uhr lokal (999 = keiner) -> Nachtfrost
    int   rain_from;  // lokale Stunde des ersten Regen-Slots (>= 0,3 mm), -1 = kein Regen
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
    bool fog_now;      // aktuell Nebel (ID 741) oder Sicht < 1 km

    bool fc_valid;     // Vorhersage gueltig
    owm_fc_day_t fc[3];// morgen, uebermorgen, Tag danach
    owm_fc_day_t today;// heutiger Tag (Min/Max + Mittags-Wetter); .valid separat,
                       // nur fuer die Telegram-Tageszusammenfassung genutzt
} owm_data_t;

// Startet den Hintergrund-Poller (OpenWeatherMap, Johannesberg).
// Aktuelle Werte alle 20 min (72/Tag), Vorhersage alle ~3 h (8/Tag) -> 80/Tag
// (unter dem 100/Tag-Limit). API-Key aus Konfig "owm_key".
void owm_init(void);

// Threadsichere Kopie der zuletzt abgefragten Werte.
void owm_get(owm_data_t *out);

// Sofortiger Abruf (aktuelle Werte + Vorhersage) im aufrufenden Task, blockiert
// bis fertig. Fuer den "jetzt aktualisieren"-Button im Web und das Telegram-
// Kommando "vorhersage". Serialisiert mit dem Hintergrund-Poller.
void owm_refresh(void);

// Client-Modus: Daten von home-central setzen.
void owm_set(const owm_data_t *d);
