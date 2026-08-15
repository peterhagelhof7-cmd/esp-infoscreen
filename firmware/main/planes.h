#pragma once
#include <stdbool.h>
#include <stddef.h>

// Flugzeuge in der Naehe des Standorts ueber die adsb.lol-API
// (https://api.adsb.lol/v2/point/<lat>/<lon>/<radius>). Kostenlos, ohne
// API-Key/Anmeldung. Gleiches v2-Format wie airplanes.live (das den offenen
// Zugang inzwischen gesperrt hat). Der Standort (Breite/Laenge) und der Radius
// (in nautischen Meilen, max. 250) sind ueber das Webinterface einstellbar
// (Config planes_lat/lon/radius).

#define PLANES_MAX 8   // so viele naechste Flugzeuge werden vorgehalten

typedef struct {
    char   flight[10];  // Callsign (Feld "flight"), leer wenn unbekannt
    char   type[8];     // Flugzeugtyp (Feld "t"), z.B. "A320"
    char   reg[10];     // Registrierung (Feld "r")
    int    alt_ft;      // barometrische Hoehe in Fuss; -1 = am Boden ("ground")
    int    gs_kt;       // Geschwindigkeit ueber Grund in Knoten (Feld "gs")
    int    track;       // Kurs ueber Grund in Grad (Feld "track"); -1 unbekannt
    double dst_nm;      // Entfernung vom Standort in nautischen Meilen
} plane_t;

typedef struct {
    bool    valid;              // letzte Abfrage erfolgreich?
    int     count;              // Anzahl gueltiger Eintraege (<= PLANES_MAX)
    plane_t ac[PLANES_MAX];     // nach Entfernung aufsteigend sortiert
} planes_data_t;

// Startet den Hintergrund-Poller.
void planes_init(void);

// Threadsichere Kopie der zuletzt abgefragten Flugzeuge.
void planes_get(planes_data_t *out);

// Sofortiger Abruf im aufrufenden Task, blockiert bis fertig. Fuer das
// Telegram-Kommando "planes". Serialisiert mit dem Hintergrund-Poller.
void planes_refresh(void);
