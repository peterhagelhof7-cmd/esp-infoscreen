#pragma once
#include <stdbool.h>

typedef struct {
    bool valid;         // Abfrage erfolgreich
    int  count;         // Anzahl aktiver Warnungen
    char headline[96];  // Kopfzeile der (ersten) Warnung
    char severity[16];  // "Minor"/"Moderate"/"Severe"/"Extreme"
} dwd_data_t;

// Startet den Hintergrund-Poller (DWD-Warnungen via Bright Sky, Johannesberg).
void dwd_init(void);

// Threadsichere Kopie der zuletzt abgefragten Warnlage.
void dwd_get(dwd_data_t *out);
