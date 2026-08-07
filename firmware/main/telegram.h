#pragma once
#include <stdbool.h>

// Telegram-Bot-Anbindung:
//  - zeigt den Chatverlauf einer Gruppe (Message Board) auf Display + Web
//  - postet DWD-Wetterwarnungen in die Gruppe
//  - reagiert auf @erwaehnung mit "Wetter"/"internet"/"termin"
//  - meldet sich nach dem Neustart mit "wieder da" + aktuellen Werten
//
// Konfig (NVS): "tg_token" (Bot-API-Token), "tg_chat" (Gruppen-Chat-ID).

#define TG_MAX_MSGS 16

typedef struct {
    char from[24];    // Vorname des Absenders (UTF-8)
    char text[192];   // Nachrichtentext (UTF-8)
} tg_msg_t;

// Startet den Poll-Task (getUpdates) und die Bot-Logik.
void telegram_init(void);

// true, wenn Token UND Chat-ID hinterlegt sind.
bool telegram_configured(void);

// Text (UTF-8) an die konfigurierte Gruppe senden. false, wenn nicht
// konfiguriert oder der Versand fehlschlug.
bool telegram_send(const char *text);

// Chatverlauf (aelteste..neueste) kopieren. Liefert die Anzahl.
int telegram_get_history(tg_msg_t *out, int max);

// Zaehler, der bei jeder neuen Nachricht hochzaehlt (fuer Refresh-Erkennung).
unsigned telegram_history_version(void);
