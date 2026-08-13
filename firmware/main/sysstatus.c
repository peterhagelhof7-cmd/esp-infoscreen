#include "sysstatus.h"
#include "network_manager.h"

#include <stdio.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_app_desc.h"

void sysstatus_text(char *buf, size_t len)
{
    int64_t secs = esp_timer_get_time() / 1000000;
    int days = (int)(secs / 86400);
    int hh   = (int)((secs % 86400) / 3600);
    int mm   = (int)((secs % 3600) / 60);
    int ss   = (int)(secs % 60);

    // Interner RAM ist der Engpass (TLS/AES-DMA, WiFi, LVGL, Stacks). Der fruehere
    // esp_get_free_heap_size() lieferte den Gesamtwert INKL. PSRAM (~7 MB) und
    // verdeckte den Mangel. Deshalb explizit MALLOC_CAP_INTERNAL: frei, Minimum
    // seit Boot und groesster zusammenhaengender Block (DMA/TLS brauchen contig.).
    size_t free_int  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t min_int   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t big_int   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_ps   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    const esp_app_desc_t *app = esp_app_get_description();

    net_status_t st;
    network_manager_get_status(&st);
    char netline[80];
    if (st.connected)
        snprintf(netline, sizeof(netline), "WLAN: %d dBm, IP %s", st.rssi, st.ip[0] ? st.ip : "-");
    else
        snprintf(netline, sizeof(netline), "WLAN: nicht verbunden");

    snprintf(buf, len,
        "Firmware: %s\n"
        "Uptime: %dT %02d:%02d:%02d\n"
        "RAM intern: %u KB frei (min %u, Block %u)\n"
        "PSRAM frei: %u KB\n"
        "%s",
        app ? app->version : "?",
        days, hh, mm, ss,
        (unsigned)(free_int / 1024), (unsigned)(min_int / 1024), (unsigned)(big_int / 1024),
        (unsigned)(free_ps / 1024),
        netline);
}
