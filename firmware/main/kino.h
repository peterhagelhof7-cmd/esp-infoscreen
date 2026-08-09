#pragma once
#include <stddef.h>

// Kinoprogramm (kino.de-Backend-API, KINOPOLIS Aschaffenburg = Cinema 1405).
// KEIN eigener Task: der Abruf laeuft aus dem Telegram-Task heraus und wird
// hoechstens einmal pro Kalendertag ausgefuehrt (taegliche Aktualisierung),
// um das knappe interne RAM-Stack-Budget nicht weiter zu belasten. Ein Slide
// gibt es bewusst nicht - die Ausgabe erfolgt nur per Bot-Kommando.

// Mutex + Cache anlegen (kein Task).
void kino_init(void);

// Aus dem Telegram-Task 1x pro Schleife aufrufen. Laedt das Programm nur, wenn
// seit dem letzten erfolgreichen Abruf ein neuer Kalendertag begonnen hat
// (bzw. noch nie geladen wurde). Ansonsten billiger No-op.
void kino_refresh_if_due(void);

// Baut die Programm-Nachricht (nur Filmtitel + FSK) in out.
// current: alle Filme mit Vorstellungen ab heute (~7 Tage, so weit die API reicht).
void kino_build_current(char *out, size_t len);
// preview: nur Filme mit Vorstellungen ab dem naechsten Montag.
void kino_build_preview(char *out, size_t len);
