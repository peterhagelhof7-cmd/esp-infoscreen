#pragma once
#include <stdbool.h>

// Amtliche Katastrophen-/Bevoelkerungsschutz-Warnungen ueber die BBK/NINA-API
// (warnung.bund.de). Buendelt MoWaS (Zivilschutz), Hochwasser (LHP), KATWARN,
// BIWAPP und DWD. Region ueber den Amtlichen Regionalschluessel (Config
// "nina_ars", 12-stellig; Default Landkreis Aschaffenburg = 096710000000).

typedef struct {
    bool valid;          // Abruf erfolgreich
    int  count;          // Anzahl aktiver Warnungen
    char headline[96];   // Kopfzeile der schwersten Warnung
    char severity[16];   // Minor / Moderate / Severe / Extreme
    char provider[12];   // MOWAS / DWD / LHP / KATWARN / BIWAPP
} nina_data_t;

void nina_init(void);
void nina_get(nina_data_t *out);
void nina_refresh(void);   // sofort neu abrufen (z.B. nach ARS-Aenderung)
