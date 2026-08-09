#include "display.h"
#include "slideshow.h"
#include "slides.h"
#include "config_store.h"
#include "network_manager.h"
#include "time_sync.h"
#include "web_server.h"
#include "ota_manager.h"
#include "fritzbox.h"
#include "http_util.h"
#include "muell.h"
#include "termine.h"
#include "dwd.h"
#include "nina.h"
#include "spessart.h"
#include "owm.h"
#include "telegram.h"

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
    display_set_brightness(config_get_int("brightness", 100));   // gespeicherte Helligkeit

    // Netzwerk VOR den Slides (Slides fragen Netzwerkstatus ab)
    network_manager_init();
    time_sync_init();
    http_util_init();  // serialisiert HTTPS-Abrufe (nur 1 TLS-Kontext gleichzeitig)

    // Webserver VOR den Pollern starten: der httpd-Task braucht einen grossen
    // zusammenhaengenden internen RAM-Block; nach dem Erzeugen aller Poller-Tasks
    // ist der Heap zu fragmentiert -> httpd_start scheitert (ESP_ERR_HTTPD_TASK).
    web_server_start();

    fritzbox_init();   // UPnP/IGD
    muell_init();      // MyMuell/jumomind
    dwd_init();        // Bright Sky (DWD-Warnungen)
    nina_init();       // BBK/NINA (Katastrophen-/Bevoelkerungsschutz)
    spessart_init();   // spessartwetter.de
    owm_init();        // OpenWeatherMap
    termine_init();    // Auto-Aufraeumung vergangener Termine
    telegram_init();   // Telegram-Bot / Message Board

    // Slideshow aufbauen: Uhr/Datum, Netzwerk, WLAN-Empfang - alle 10 s wechseln
    slideshow_init(disp);
    slides_register_all();
    int sec = config_get_int("slide_sec", 10);
    if (sec < 3) sec = 3;
    if (sec > 120) sec = 120;
    slideshow_start((uint32_t)sec * 1000);

    // Init erfolgreich -> Rollback abbrechen (frisch geflashte App gilt als ok)
    ota_manager_mark_valid();

    ESP_LOGI(TAG, "Init abgeschlossen");
}
