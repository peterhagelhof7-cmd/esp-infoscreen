#pragma once
#include <stdbool.h>

// Startet die NTP-Synchronisation (Zeitzone Deutschland, CET/CEST). Synchronisiert
// automatisch, sobald Netzwerk verfuegbar ist.
void time_sync_init(void);

// true, sobald die Uhr einmal per NTP gestellt wurde.
bool time_sync_is_valid(void);
