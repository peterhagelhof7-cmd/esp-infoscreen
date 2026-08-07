#pragma once
#include <stdbool.h>

typedef struct {
    bool valid;
    char temp[16];   // z.B. "19,8" (Grad C)
    char wind[16];   // z.B. "4,6"  (km/h)
} spessart_data_t;

// Startet den Hintergrund-Poller (spessartwetter.de custom.html, alle 30 min).
void spessart_init(void);

// Threadsichere Kopie der zuletzt gescrapten Werte.
void spessart_get(spessart_data_t *out);
