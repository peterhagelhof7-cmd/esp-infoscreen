#include "display.h"
#include "status_screen.h"
#include "config_store.h"
#include "network_manager.h"
#include "web_server.h"
#include "ota_manager.h"

#include "esp_log.h"
#include "esp_psram.h"

static const char *TAG = "infoscreen";

void app_main(void)
{
    ESP_LOGI(TAG, "esp-infoscreen startet");
    ESP_LOGI(TAG, "PSRAM: %u Bytes", (unsigned)esp_psram_get_size());

    ota_manager_init();   // Boot-Status/laufende Partition loggen

    // Persistenz + Display
    config_store_init();
    lv_display_t *disp = display_init();
    status_screen_create(disp);

    // Netzwerk: STA mit gespeicherten Daten, sonst Installer-AP; Web-Konfig + OTA
    network_manager_init();
    web_server_start();

    // Alles initialisiert -> laufende App als gueltig markieren (Bootloader-
    // Rollback abbrechen). Kommt eine frisch per OTA geflashte App bis hierher,
    // gilt sie als funktionsfaehig; stuerzt sie vorher ab, rollt der Bootloader
    // beim naechsten Neustart auf die vorige App zurueck.
    ota_manager_mark_valid();

    ESP_LOGI(TAG, "Init abgeschlossen");
}
