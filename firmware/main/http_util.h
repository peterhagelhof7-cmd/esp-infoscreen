#pragma once
#include <stddef.h>

// Einfacher HTTP(S)-GET. Schreibt den Body (nullterminiert) nach buf.
// Liefert die Anzahl Bytes (>=0) oder -1 bei Fehler / Status != 200.
// HTTPS wird ueber das ESP-IDF-Zertifikatsbundle verifiziert.
int http_get(const char *url, char *buf, size_t buf_len);
