#pragma once
#include <stdbool.h>

// Registriert die aktivierten Slides in der Slideshow (Config "sl_<id>").
void slides_register_all(void);

// Katalog aller vorhandenen Slides (fuer die Web-Auswahl).
int         slides_catalog_count(void);
const char *slides_catalog_id(int i);
const char *slides_catalog_title(int i);
bool        slides_catalog_enabled(int i);   // laut Config (Default: an)
