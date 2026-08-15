#include "feiertage.h"
#include "config_store.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

// Bundesland-Codes -> Bit-Index. Reihenfolge fix (die Masken unten beziehen
// sich darauf): Bit 0 = BW, Bit 1 = BY, ... Bit 15 = TH.
static const char *BL_CODES[16] = {
    "BW","BY","BE","BB","HB","HH","HE","MV","NI","NW","RP","SL","SN","ST","SH","TH"
};

static int bl_index(const char *code)
{
    for (int i = 0; i < 16; i++)
        if (strcmp(code, BL_CODES[i]) == 0) return i;
    return -1;
}

// Ostersonntag (gregorianisch, Meeus/Butcher) -> Monat/Tag.
static void easter(int y, int *mon, int *day)
{
    int a = y % 19, b = y / 100, c = y % 100;
    int d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    *mon = (h + l - 7 * m + 114) / 31;
    *day = ((h + l - 7 * m + 114) % 31) + 1;
}

// Datum aus (y,m,d)+off Tagen normalisiert als "YYYY-MM-DD".
static void mkdate(int y, int m, int d, int off, char *out, size_t len)
{
    struct tm tm; memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d + off;
    tm.tm_hour = 12; tm.tm_isdst = -1;
    mktime(&tm);
    strftime(out, len, "%Y-%m-%d", &tm);
}

// Wochentag (0=So .. 6=Sa) fuer (y,m,d).
static int weekday(int y, int m, int d)
{
    struct tm tm; memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
    tm.tm_hour = 12; tm.tm_isdst = -1;
    mktime(&tm);
    return tm.tm_wday;
}

// Feste Feiertage: Monat, Tag, Bundesland-Maske, Name.
typedef struct { int m, d; uint16_t mask; const char *name; } fix_t;
// Osterabhaengige Feiertage: Offset zu Ostersonntag, Maske, Name.
typedef struct { int off; uint16_t mask; const char *name; } mov_t;

#define ALL 0xFFFF

static int fill_year(int year, int blbit, feiertag_t *out, int max, int n)
{
    static const fix_t FIX[] = {
        {  1,  1, ALL,    "Neujahr" },
        {  1,  6, 0x2003, "Heilige Drei Könige" },      // BW, BY, ST
        {  3,  8, 0x0084, "Frauentag" },                // BE, MV
        {  5,  1, ALL,    "Tag der Arbeit" },
        {  8, 15, 0x0802, "Mariä Himmelfahrt" },        // BY (kath.), SL
        {  9, 20, 0x8000, "Weltkindertag" },            // TH
        { 10,  3, ALL,    "Tag der Deutschen Einheit" },
        { 10, 31, 0xF1B8, "Reformationstag" },          // BB,HB,HH,MV,NI,SN,ST,SH,TH
        { 11,  1, 0x0E03, "Allerheiligen" },            // BW,BY,NW,RP,SL
        { 12, 25, ALL,    "1. Weihnachtstag" },
        { 12, 26, ALL,    "2. Weihnachtstag" },
    };
    static const mov_t MOV[] = {
        { -2, ALL,    "Karfreitag" },
        {  1, ALL,    "Ostermontag" },
        { 39, ALL,    "Christi Himmelfahrt" },
        { 50, ALL,    "Pfingstmontag" },
        { 60, 0x0E43, "Fronleichnam" },                 // BW,BY,HE,NW,RP,SL
    };

    for (size_t i = 0; i < sizeof(FIX) / sizeof(FIX[0]) && n < max; i++) {
        if (!(FIX[i].mask & blbit)) continue;
        mkdate(year, FIX[i].m, FIX[i].d, 0, out[n].date, sizeof(out[n].date));
        snprintf(out[n].name, sizeof(out[n].name), "%s", FIX[i].name);
        n++;
    }
    int em, ed; easter(year, &em, &ed);
    for (size_t i = 0; i < sizeof(MOV) / sizeof(MOV[0]) && n < max; i++) {
        if (!(MOV[i].mask & blbit)) continue;
        mkdate(year, em, ed, MOV[i].off, out[n].date, sizeof(out[n].date));
        snprintf(out[n].name, sizeof(out[n].name), "%s", MOV[i].name);
        n++;
    }
    // Buß- und Bettag (nur SN, Bit 12): Mittwoch vor dem 23. November.
    if ((0x1000 & blbit) && n < max) {
        int d = 22;
        while (weekday(year, 11, d) != 3) d--;   // 3 = Mittwoch
        mkdate(year, 11, d, 0, out[n].date, sizeof(out[n].date));
        snprintf(out[n].name, sizeof(out[n].name), "Buß- und Bettag");
        n++;
    }
    return n;
}

int feiertage_get(feiertag_t *out, int max)
{
    char bl[8];
    config_get_str_def("feiertage_bl", bl, sizeof(bl), "BY");
    int idx = bl_index(bl);
    if (idx < 0) return 0;              // leer / "aus" / ungueltig
    int blbit = 1 << idx;

    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_year <= 120) return 0;   // Uhr steht noch nicht (kein NTP)
    int year = tm.tm_year + 1900;

    int n = 0;
    n = fill_year(year,     blbit, out, max, n);
    n = fill_year(year + 1, blbit, out, max, n);   // Jahreswechsel abdecken
    return n;
}
