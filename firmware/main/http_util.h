#pragma once
#include <stddef.h>
#include <stdbool.h>

// Einmalig vor dem ersten http_get() aufrufen (erzeugt die Serialisierungssperre).
void http_util_init(void);

// Download-Wächter: fuellt out mit einer Meldung, wenn eine Quelle (Host) seit
// >1h nicht mehr erfolgreich abgerufen werden konnte (aber weiter versucht wird),
// oder sich nach einem Ausfall wieder erholt hat. Liefert true, wenn out zu
// senden ist. Jeder Ausfall/jede Erholung wird nur einmal gemeldet.
bool httphealth_alert(char *out, size_t len);

// Einfacher HTTP(S)-GET. Schreibt den Body (nullterminiert) nach buf.
// Liefert die Anzahl Bytes (>=0) oder -1 bei Fehler / Status != 200.
// HTTPS wird ueber das ESP-IDF-Zertifikatsbundle verifiziert.
int http_get(const char *url, char *buf, size_t buf_len);
