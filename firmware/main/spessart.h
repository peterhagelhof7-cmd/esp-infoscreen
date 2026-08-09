#pragma once
#include <stdbool.h>

typedef struct {
    bool valid;
    char temp[16];   // z.B. "19,8" (Grad C)
    char wind[16];   // z.B. "4,6"  (km/h) - mittlere Windgeschwindigkeit
    char gust[16];   // z.B. "19,3" (km/h) - Windboeen (aktuell)
} spessart_data_t;

// Startet den Hintergrund-Poller (spessartwetter.de custom.html, alle 30 min).
void spessart_init(void);

// Threadsichere Kopie der zuletzt gescrapten Werte.
void spessart_get(spessart_data_t *out);

// Boeen als Zahl (km/h) aus dem String (Komma->Punkt). 0, wenn leer/ungueltig.
double spessart_gust_kmh(const spessart_data_t *d);
