#pragma once
#include <stdbool.h>

// Kurse-Poller: Bitcoin (BTC-USD) und EUR-USD inkl. 24-Stunden-Trend.
// Eine einzige CoinGecko-Abfrage (kein API-Key) liefert BTC in USD und EUR mit
// je 24h-Aenderung; EUR-USD wird als BTC-Cross (usd/eur) abgeleitet, die
// 24h-Aenderung des Cross naeherungsweise als Differenz der beiden Aenderungen.

typedef struct {
    bool   valid;
    double btc_usd;   // BTC-USD Kurs
    double btc_chg;   // 24h-Aenderung BTC-USD in Prozent
    double eur_usd;   // EUR-USD Cross-Kurs
    double eur_chg;   // 24h-Aenderung EUR-USD in Prozent (Naeherung)
} kurse_data_t;

// Startet den Hintergrund-Poller (1 Abruf alle 5 min).
void kurse_init(void);

// Threadsichere Kopie der zuletzt abgefragten Werte.
void kurse_get(kurse_data_t *out);
