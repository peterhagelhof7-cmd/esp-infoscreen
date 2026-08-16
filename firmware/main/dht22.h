#pragma once
#include <stdbool.h>

// DHT22-Innenraumsensor am P4-Anschluss (GPIO18, siehe board_config.h).
// Bit-Banging-Treiber (kein RMT); Aufruf nur ueber den Poller, nicht direkt
// aus dem UI-Task (Lesevorgang dauert ~5 ms und sperrt kurz Interrupts).

typedef struct {
    bool  valid;
    float temp_c;    // Grad Celsius
    float hum_pct;   // relative Luftfeuchte in %
} dht22_data_t;

void dht22_init(void);                    // GPIO konfigurieren + Poller-Task starten
void dht22_get(dht22_data_t *out);        // letzten gueltigen Messwert liefern (Thread-sicher)

// Verlauf: ein Datenpunkt alle 30 min, 5 h Fenster = 11 Punkte.
#define DHT22_HIST_LEN 11
// Kopiert die Verlaufswerte (aeltester zuerst) nach temp[]/hum[]; liefert die
// Anzahl der gueltigen Punkte (<= max). Thread-sicher.
int  dht22_history_get(float *temp, float *hum, int max);
