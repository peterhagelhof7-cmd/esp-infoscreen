#pragma once
#include <stdbool.h>

// Kurse-Poller: Bitcoin (BTC-EUR) und EUR-USD, Basis immer Euro.
// Quelle: Bitstamp Public-Ticker (DigiCert-Zertifikat -> vom ESP-crt_bundle
// verifizierbar, keyless, hohes Rate-Limit). Zwei Abrufe (btceur + btcusd), je
// mit last + open_24 + percent_change_24, liefern sofort echte 24h-Werte:
// BTC-EUR direkt, EUR-USD als BTC-Cross (btcusd/btceur). has_trend ist daher
// gueltig, sobald valid (kein 24h-Warmup wie beim frueheren Selbst-Tracking).

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
