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
#include "kino.h"
#include "dht22.h"

#include <stdlib.h>
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "infoscreen";

// cJSON-Allokationen ins PSRAM verlagern: die JSON-Parser (muell/nina/telegram/
// kino) erzeugen viele kleine Knoten; im knappen internen Heap kann das beim
// grossen Kino-Abruf (~44 KB) eng werden. free() funktioniert unter ESP-IDF
// fuer beide Heap-Regionen, daher genuegt der PSRAM-malloc-Hook.
static void *json_psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp-infoscreen startet");
    ESP_LOGI(TAG, "PSRAM: %u Bytes", (unsigned)esp_psram_get_size());

    // cJSON-Speicher ins PSRAM legen (vor dem ersten Parse durch die Poller).
    cJSON_Hooks jh = { .malloc_fn = json_psram_malloc, .free_fn = free };
    cJSON_InitHooks(&jh);

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

    // Grosse Tasks (httpd, telegram) VOR den vielen Pollern erzeugen: sie
    // brauchen einen grossen zusammenhaengenden internen RAM-Block; nach dem
    // Erzeugen aller Poller-Tasks ist der Heap zu fragmentiert -> xTaskCreate
    // scheitert (httpd: ESP_ERR_HTTPD_TASK; telegram: Task laeuft nie).
    web_server_start();
    kino_init();       // Kino-Cache (kein Task; Refresh laeuft im Telegram-Task)
    telegram_init();   // Telegram-Bot / Message Board

    fritzbox_init();   // UPnP/IGD
    muell_init();      // MyMuell/jumomind
    dwd_init();        // Bright Sky (DWD-Warnungen)
    nina_init();       // BBK/NINA (Katastrophen-/Bevoelkerungsschutz)
    spessart_init();   // spessartwetter.de
    owm_init();        // OpenWeatherMap
    termine_init();    // Auto-Aufraeumung vergangener Termine
    dht22_init();      // Innenraumsensor (P4, GPIO18)

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
