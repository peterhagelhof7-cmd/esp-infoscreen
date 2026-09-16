#pragma once
#include <stdbool.h>

// Kurse-Poller: Bitcoin (BTC-EUR) und EUR-USD, Basis immer Euro.
// Quelle: Coinbase exchange-rates (keyless, kein Rate-Limit-Problem). Ein Aufruf
// liefert EUR->USD und EUR->BTC; BTC-EUR = 1/rate(BTC). Die 24h-Aenderung liefert
// Coinbase nicht mit, daher wird sie geraeteseitig aus stuendlichen Snapshots
// selbst berechnet (Baseline ~24h alt). Nach einem Neustart ist der Trend erst
// wieder aussagekraeftig, sobald 24h Historie vorliegt (bis dahin has_trend=false).

typedef struct {
    bool   valid;
    double btc_eur;    // 1 BTC in EUR
    double eur_usd;    // 1 EUR in USD
    bool   has_trend;  // true, sobald eine ~24h-alte Baseline vorliegt
    double btc_chg;    // 24h-Aenderung BTC-EUR in Prozent (nur gueltig bei has_trend)
    double eur_chg;    // 24h-Aenderung EUR-USD in Prozent (nur gueltig bei has_trend)
} kurse_data_t;

// Startet den Hintergrund-Poller (1 Abruf alle 5 min).
void kurse_init(void);

// Threadsichere Kopie der zuletzt abgefragten Werte.
void kurse_get(kurse_data_t *out);
