#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Weltraum-Daten: nachts sichtbare Satelliten-Ueberfluege (N2YO /visualpasses,
// kuratierte Liste heller Objekte) + erdnahe Objekte/Asteroiden (NASA NeoWs).
// Ein gemeinsamer Hintergrund-Poller-Task (spart internen RAM). API-Keys ueber
// die Web-Config: "n2yo_key" und "nasa_key". Standort aus "planes_lat"/
// "planes_lon" (wiederverwendet), Beobachterhoehe aus "space_alt" (m).

#define SPACE_MAX_PASSES 8   // so viele naechste sichtbare Ueberfluege
#define SPACE_MAX_NEOS   8   // so viele naechste erdnahe Objekte

typedef struct {
    char   name[24];     // Satellitenname (N2YO satname, gekuerzt)
    time_t max_utc;      // Zeitpunkt der groessten Hoehe (UTC, Unix)
    int    max_el;       // max. Elevation ueber Horizont (Grad)
    char   dir[4];       // Richtung am Kulminationspunkt (Kompass)
    double mag;          // scheinbare Helligkeit (kleiner = heller)
} sat_pass_t;

typedef struct {
    char   name[28];     // Objektname (NeoWs)
    double miss_km;      // dichtester Abstand zur Erde (km)
    double diam_m;       // geschaetzter Durchmesser (m, Mittelwert)
    bool   hazard;       // von NASA als potentiell gefaehrlich markiert
    time_t approach_utc; // Zeitpunkt der dichtesten Annaeherung (UTC, Unix)
} neo_t;

typedef struct {
    bool       valid;    // letzte N2YO-Abfrage erfolgreich (Key gesetzt)
    bool       has_key;  // n2yo_key konfiguriert?
    int        count;    // Anzahl gefuellter Ueberfluege (<= SPACE_MAX_PASSES)
    sat_pass_t p[SPACE_MAX_PASSES];   // zeitlich aufsteigend sortiert
} sat_passes_t;

typedef struct {
    bool  valid;         // letzte NeoWs-Abfrage erfolgreich (Key gesetzt)
    bool  has_key;       // nasa_key konfiguriert?
    int   count;         // Anzahl gefuellter Objekte (<= SPACE_MAX_NEOS)
    neo_t n[SPACE_MAX_NEOS];   // nach Abstand aufsteigend sortiert
} neos_t;

// Startet den Hintergrund-Poller (Satelliten ~alle 2 h, Asteroiden ~alle 6 h).
void space_init(void);

// Threadsichere Kopien (Bot/Slide nutzen diese gecachten Daten).
void space_get_passes(sat_passes_t *out);
void space_get_neos(neos_t *out);

// Poller sofort neu abrufen lassen (nicht-blockierend, per Task-Notify) -
// z.B. nachdem im Web ein API-Key gesetzt wurde.
void space_wake(void);
