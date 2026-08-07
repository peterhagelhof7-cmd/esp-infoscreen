#include "display.h"
#include "slideshow.h"
#include "slides.h"
#include "config_store.h"
#include "network_manager.h"
#include "time_sync.h"
#include "web_server.h"
#include "ota_manager.h"
#include "fritzbox.h"
#include "muell.h"
#include "dwd.h"
#include "spessart.h"

#include "esp_log.h"
#include "esp_psram.h"

static const char *TAG = "infoscreen";

void app_main(void)
{
    ESP_LOGI(TAG, "esp-infoscreen startet");
    ESP_LOGI(TAG, "PSRAM: %u Bytes", (unsigned)esp_psram_get_size());

    ota_manager_init();
    config_store_init();

    // Gespeicherte Anzeige-Drehung (Deckenmontage 180 Grad) beim Init anwenden
    char rot[4];
    bool rot180 = config_get_str("rot180", rot, sizeof(rot)) && rot[0] == '1';
    lv_display_t *disp = display_init(rot180);

    // Netzwerk VOR den Slides (Slides fragen Netzwerkstatus ab)
    network_manager_init();
    time_sync_init();
    fritzbox_init();   // UPnP/IGD
    muell_init();      // MyMuell/jumomind
    dwd_init();        // Bright Sky (DWD-Warnungen)
    spessart_init();   // spessartwetter.de

    // Slideshow aufbauen: Uhr/Datum, Netzwerk, WLAN-Empfang - alle 10 s wechseln
    slideshow_init(disp);
    slides_register_all();
    slideshow_start(10000);

    // Web-Konfig + OTA
    web_server_start();

    // Init erfolgreich -> Rollback abbrechen (frisch geflashte App gilt als ok)
    ota_manager_mark_valid();

    ESP_LOGI(TAG, "Init abgeschlossen");
}
