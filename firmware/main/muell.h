#pragma once
#include <stdbool.h>

typedef struct {
    char day[11];    // "YYYY-MM-DD"
    char title[24];  // z.B. "Bio Müll", "Restmüll", "Gelber Sack"
} muell_entry_t;

// Startet den Hintergrund-Poller (MyMuell/jumomind, Johannesberg-Oberafferbach).
void muell_init(void);

// Kopiert die naechsten (heutigen/zukuenftigen) Termine, bis max. Liefert Anzahl.
int muell_get(muell_entry_t *out, int max);
