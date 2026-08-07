#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// OTA-Firmware-Update mit Bootloader-Rollback.
// Ablauf: ota_manager_begin() -> mehrfach ota_manager_write() -> ota_manager_finish().
// Bei Fehlern ota_manager_abort(). Nach erfolgreichem Boot ota_manager_mark_valid()
// aufrufen, damit der Bootloader nicht auf die alte App zurueckrollt.

void ota_manager_init(void);       // loggt laufende Partition + Boot-Status
bool ota_manager_begin(void);      // naechsten Slot vorbereiten, esp_ota_begin
bool ota_manager_write(const uint8_t *data, size_t len);
bool ota_manager_finish(void);     // esp_ota_end + Boot-Partition setzen (KEIN Neustart)
void ota_manager_abort(void);      // laufendes Update verwerfen
void ota_manager_mark_valid(void); // Rollback abbrechen (App ist ok)
