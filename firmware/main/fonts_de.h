#pragma once
#include "lvgl.h"

// Eigene Montserrat-Fonts (aus der originalen Montserrat-Medium.ttf per
// lv_font_conv erzeugt) MIT deutschen Umlauten (ae/oe/ue/ss -> echte ae/oe/ue
// Glyphen) und Grad-Zeichen. Ersetzen die eingebauten lv_font_montserrat_*,
// die keine Latin-1-Sonderzeichen enthalten.
extern const lv_font_t montserrat_de_14;
extern const lv_font_t montserrat_de_28;
extern const lv_font_t montserrat_de_48;
