#pragma once
#include <stdbool.h>

// Heizung (Froeling ueber esp-p4d): kommt nur ueber home-central (kein eigener
// Abruf). home-central liefert im Paket /api/v1/screen den Block "heizung" mit
// bereits formatierten Werten ("72,5 °C") - der Infoscreen zeigt nur an.

#define HZ_MAX_VALUES 12

typedef struct {
    char name[40];
    char value[24];
} hz_value_t;

typedef struct {
    bool configured;            // home-central hat die Heizung eingerichtet
    bool reachable;             // home-central erreicht das esp-p4d
    bool online;                // Kessel antwortet dem esp-p4d auf COM1
    int  age;                   // s seit dem letzten erfolgreichen Abruf (-1 = nie)
    char mode[32];              // "Automatik"
    char state[48];             // "Heizen"
    char error[64];             // Stoerungstext, leer = keine
    int  n;
    hz_value_t v[HZ_MAX_VALUES];
} heizung_data_t;

void heizung_init(void);
void heizung_get(heizung_data_t *out);
// Client-Modus: Daten von home-central setzen.
void heizung_set(const heizung_data_t *d);
