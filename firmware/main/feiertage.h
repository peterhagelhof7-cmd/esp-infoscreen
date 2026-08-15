#pragma once
#include <stddef.h>

// Gesetzliche Feiertage Deutschlands, lokal berechnet (kein Netz): feste Termine
// + osterabhaengige Tage (Meeus/Butcher). Das Bundesland kommt aus der Config
// "feiertage_bl" (2-Buchstaben-Code, z.B. "BY"; leer/ungueltig = aus). Die
// Feiertage werden wie die Muellabfuhr in den Terminplan eingemischt.
//
// Hinweis: Nur landesweite gesetzliche Feiertage. Rein kommunale Tage (z.B.
// Augsburger Friedensfest, Fronleichnam in einzelnen Gemeinden) sind bewusst
// nicht enthalten, da sie sich nicht am Bundesland festmachen lassen.

typedef struct {
    char date[11];   // YYYY-MM-DD
    char name[40];   // Feiertagsname (UTF-8)
} feiertag_t;

// Fuellt die Feiertage des gewaehlten Bundeslandes fuer das aktuelle UND naechste
// Jahr (deckt den Jahreswechsel ab) in out. Liefert die Anzahl (0, wenn kein/
// ungueltiges Bundesland oder die Uhr noch nicht per NTP steht).
int feiertage_get(feiertag_t *out, int max);
