#pragma once
#include <stdbool.h>
#include <stddef.h>

// home-central als Datenquelle (Client-Modus).
//
// Config "data_src": "auto" (Default) | "direct" | "hc".
//  - Client-Modus, wenn data_src != "direct" UND ein Token ("hc_token") sowie
//    eine Server-Adresse ("hc_url") gespeichert sind. Dann laufen KEINE eigenen
//    Abrufe und KEIN Telegram-Bot; alle Daten kommen von home-central
//    (GET /api/v1/screen), die Uhr per NTP von home-central.
//  - Sonst Direkt-Modus wie bisher. Bei "auto"/"hc" sucht ein Hintergrund-Task
//    home-central per mDNS (_home-central._tcp) und fragt per /api/v1/pair die
//    Kopplung an. Nach der Freigabe in der Weboberflaeche von home-central
//    werden Adresse + Token gespeichert und das Geraet startet im Client-Modus.
// Die Betriebsart wird nur beim Start bestimmt -> nie beide Quellen gleichzeitig.

typedef enum { HC_MODE_DIRECT = 0, HC_MODE_CLIENT = 1 } hc_mode_t;

// Nach config_store_init(), vor allen Modul-Inits aufrufen.
void hc_client_early_init(void);
hc_mode_t hc_mode(void);

// Host fuer SNTP im Client-Modus (z.B. "192.168.79.1"). false = Direkt-Modus.
bool hc_client_ntp_host(char *out, size_t len);

// Nach network_manager_init(): startet den Client- bzw. Such-/Kopplungs-Task.
void hc_client_start(void);

// Kurzstatus fuer die Weboberflaeche (HTML-sicher).
void hc_client_status(char *out, size_t len);

// Message Board im Client-Modus: Text ueber den Bot von home-central senden.
bool hc_client_tg_send(const char *text);
