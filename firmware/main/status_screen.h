#pragma once
#include "lvgl.h"

// Baut eine einfache Statusseite (WLAN-Modus, SSID, IP, Empfang) und
// aktualisiert sie periodisch. Vorlaeufer der spaeteren Slideshow.
void status_screen_create(lv_display_t *disp);
