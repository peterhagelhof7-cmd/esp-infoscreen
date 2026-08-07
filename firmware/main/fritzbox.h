#pragma once
#include <stdbool.h>
#include <stdint.h>

// Fragt Werte einer Fritzbox ueber UPnP/IGD (TR-064-Geraetebaum, Port 49000)
// ab - ohne Anmeldung. Standard-Adresse ist das Gateway; ueber die Konfig
// (Schluessel "fb_host") laesst sich eine abweichende Fritzbox angeben, falls
// sie nicht das Gateway ist.

typedef struct {
    bool     reachable;       // letzte Abfrage erfolgreich?
    char     external_ip[40]; // externe IP
    uint32_t down_max_bps;    // Layer1 Downstream max (bit/s)
    uint32_t up_max_bps;      // Layer1 Upstream max (bit/s)
    uint32_t down_rate_Bps;   // aktueller Empfang (BYTE/s)
    uint32_t up_rate_Bps;     // aktuelles Senden (BYTE/s)
} fritzbox_data_t;

// Startet den Hintergrund-Poller (fragt periodisch ab).
void fritzbox_init(void);

// Threadsichere Kopie der zuletzt abgefragten Werte.
void fritzbox_get(fritzbox_data_t *out);
