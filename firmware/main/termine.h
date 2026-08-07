#pragma once
#include <stdbool.h>

typedef struct {
    char date[11];   // "YYYY-MM-DD"
    char time[6];    // "HH:MM" oder leer
    char title[40];
} termine_entry_t;

// Vom Nutzer ueber das Webinterface angelegte Termine (persistent in NVS,
// JSON-Array unter dem Schluessel "termine").

// Alle gespeicherten Termine holen (unsortiert, wie gespeichert). Liefert Anzahl.
int  termine_get_all(termine_entry_t *out, int max);

// Termin hinzufuegen (title darf nicht leer sein). true bei Erfolg.
bool termine_add(const char *date, const char *time, const char *title);

// Termin an Position idx loeschen.
void termine_delete(int idx);

// Startet die automatische Aufraeumung (loescht vergangene Termine beim Start
// und danach periodisch). Braucht keine gueltige Zeit beim Aufruf.
void termine_init(void);

// Alle Termine mit Datum vor heute loeschen. Liefert die Anzahl entfernter
// Eintraege (0, wenn die Zeit noch nicht synchronisiert ist).
int termine_purge_past(void);
