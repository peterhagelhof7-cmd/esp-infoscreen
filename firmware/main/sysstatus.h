#pragma once
#include <stddef.h>

// Schreibt einen mehrzeiligen Status-Text (UTF-8) nach buf:
// Firmware-Version, Uptime, freier Heap (intern + Minimum), freies PSRAM,
// WLAN (RSSI/IP). Wird im Webinterface und vom Telegram-Bot ("status") genutzt.
void sysstatus_text(char *buf, size_t len);

// Schreibt je Task die minimale freie Stack-Reserve seit Boot (Bytes) nach buf.
// Diagnose, um Task-Stackgroessen datenbasiert zu trimmen (Bot-Kommando "stacks").
// Benoetigt CONFIG_FREERTOS_USE_TRACE_FACILITY.
void sysstatus_stacks(char *buf, size_t len);
