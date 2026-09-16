#pragma once
#include <stddef.h>

// Ring-Puffer, der die ESP_LOG-Ausgabe im PSRAM mitschneidet (die letzten ~16 KB),
// damit sie ueber die Weboberflaeche als Textdatei heruntergeladen werden kann
// (headless-Geraet ohne serielle Konsole). Die serielle Ausgabe bleibt erhalten.
// Frueh in app_main() aufrufen, damit auch Boot-Logs erfasst werden.
void logbuf_init(void);

// Kopiert den aktuellen Loginhalt (aeltestes zuerst) nach out. Liefert die Anzahl
// geschriebener Bytes (ohne Nullterminierung).
size_t logbuf_get(char *out, size_t out_len);
