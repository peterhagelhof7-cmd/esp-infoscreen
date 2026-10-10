#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char day[11];    // "YYYY-MM-DD"
    char title[24];  // z.B. "Bio Müll", "Restmüll", "Gelber Sack"
} muell_entry_t;

// Datenquelle (Config "muell_src"). Wird nur beim Start gelesen; ein Wechsel
// erfordert einen Neustart -> es wird immer nur EINE Quelle abgefragt.
typedef enum {
    MUELL_SRC_MYMUELL = 0,   // MyMuell/jumomind (Config "muell_id")
    MUELL_SRC_AWIDO   = 1,   // AWIDO/cubefour "Abfall-App" (Config "muell_aw_cl"/"muell_aw_id")
} muell_src_t;

// Config-Defaults (Johannesberg-Oberafferbach, Landkreis Aschaffenburg).
#define MUELL_DEF_SRC      "mymuell"
#define MUELL_DEF_ID       "44886"
#define MUELL_DEF_AW_CL    "lra-ab"
#define MUELL_DEF_AW_ID    "4aa475dd-3890-45c2-8909-2a75dd1a7700"
#define MUELL_DEF_AW_ORT   "Johannesberg"
#define MUELL_DEF_AW_LBL   "Landkreis Aschaffenburg / Johannesberg / Oberafferbach"

// Startet den Hintergrund-Poller fuer die konfigurierte Quelle.
void muell_init(void);

// Kopiert die naechsten (heutigen/zukuenftigen) Termine, bis max. Liefert Anzahl.
int muell_get(muell_entry_t *out, int max);

// Sofort neu abrufen (gleiche Quelle/ID).
void muell_refresh(void);

// Client-Modus: Termine von home-central setzen (bereits gefiltert/sortiert).
void muell_set(const muell_entry_t *list, int n);

// Beim Start gewaehlte Quelle + Kurzstatus ("AWIDO: 48 Termine, Abruf 14:05").
muell_src_t muell_source(void);
void muell_status(char *out, size_t len);

// AWIDO-Mandantenkatalog (fest in der Firmware; es gibt keine Listen-API).
int muell_awido_catalog_count(void);
const char *muell_awido_catalog_id(int i);
const char *muell_awido_catalog_title(int i);

// Mandant ([a-z0-9-], max 32) und ID (GUID-Zeichen, max 40) syntaktisch gueltig?
bool muell_awido_valid(const char *client, const char *id);

// AWIDO-Nachschlag fuer die Weboberflaeche (Gemeinde -> Ortsteil/Strasse ->
// Hausnummer). step = "places" | "streets" | "addons"; client/id werden
// validiert (nur [a-z0-9-] bzw. GUID-Zeichen). Schreibt die JSON-Antwort von
// AWIDO nach buf. Liefert Laenge >= 0 oder -1 (ungueltig/Fehler/zu gross).
int muell_awido_lookup(const char *step, const char *client, const char *id,
                       char *buf, size_t len);
