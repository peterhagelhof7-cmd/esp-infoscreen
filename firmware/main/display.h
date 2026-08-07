#pragma once
#include "lvgl.h"

// Initialisiert das RGB-Panel + Hintergrundbeleuchtung + LVGL-Port.
// Liefert die LVGL-Display-Instanz. Blockiert kurz waehrend der Panel-Init.
lv_display_t *display_init(void);

// Setzt die Display-Drehung (0 oder 180 Grad). Wird spaeter vom Webinterface
// gesteuert (Deckenmontage). true = 180 Grad.
void display_set_rotated_180(lv_display_t *disp, bool rotated);
