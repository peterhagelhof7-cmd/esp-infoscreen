#pragma once
#include "lvgl.h"

// Initialisiert das RGB-Panel + Hintergrundbeleuchtung + LVGL-Port.
// rot180 = true dreht die Anzeige per HW-Spiegelung um 180 Grad (Deckenmontage).
// Die Drehung wird beim Init festgelegt (HW-State des RGB-Panels); ein Wechsel
// zur Laufzeit erfordert einen Neustart. Liefert die LVGL-Display-Instanz.
lv_display_t *display_init(bool rot180);
