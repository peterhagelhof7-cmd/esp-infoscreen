#pragma once
#include "lvgl.h"

// Einfaches Slideshow-Framework: rotiert registrierte Slides im Intervall.
// Jeder Slide baut seinen Inhalt in den uebergebenen Container (build) und kann
// waehrend er aktiv ist einmal pro Sekunde aktualisiert werden (update).

typedef struct {
    const char *id;                    // stabile Kennung fuer die Ein/Aus-Konfig
    const char *title;                 // Ueberschrift oben
    void (*build)(lv_obj_t *parent);   // Inhalt in parent aufbauen (bei jedem Anzeigen)
    void (*update)(void);              // optional: jede Sekunde waehrend aktiv (kann NULL sein)
} slide_t;

void slideshow_init(lv_display_t *disp);          // Grundgeruest (Titel/Inhalt/Fusszeile)
void slideshow_add(const slide_t *slide);         // Slide registrieren
void slideshow_start(uint32_t interval_ms);       // Rotation starten
