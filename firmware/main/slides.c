#include "slides.h"
#include "slideshow.h"
#include "network_manager.h"
#include "time_sync.h"
#include "fritzbox.h"

#include <stdio.h>
#include <time.h>
#include "lvgl.h"

// Groesste eingebaute LVGL-Schrift = Montserrat 48. Fuer die "Helden"-Werte.
#define FONT_BIG   (&lv_font_montserrat_48)
#define FONT_MED   (&lv_font_montserrat_28)

// ============================ Slide 1: Uhr / Datum ===========================
// Ohne Sekunden: nur bei Minutenwechsel neu zeichnen -> kaum Neuzeichnungen
// (reduziert Flackern). Wochentag + Uhrzeit (HH:MM) + Datum, gross.
static lv_obj_t *cl_wday, *cl_time, *cl_date;
static int cl_last_min = -1;

static const char *WD[] = { "Sonntag", "Montag", "Dienstag", "Mittwoch",
                            "Donnerstag", "Freitag", "Samstag" };

static void clock_update(void)
{
    if (!cl_time) return;
    if (!time_sync_is_valid()) {
        lv_label_set_text(cl_wday, "");
        lv_label_set_text(cl_time, "--:--");
        lv_label_set_text(cl_date, "Warte auf Zeitserver ...");
        cl_last_min = -1;
        return;
    }
    time_t now = 0; time(&now);
    struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_min == cl_last_min) return;   // nur bei Minutenwechsel neu zeichnen
    cl_last_min = tm.tm_min;

    lv_label_set_text(cl_wday, WD[tm.tm_wday % 7]);
    char t[32]; snprintf(t, sizeof(t), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(cl_time, t);
    char d[48]; snprintf(d, sizeof(d), "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    lv_label_set_text(cl_date, d);
}

static void clock_build(lv_obj_t *p)
{
    cl_wday = lv_label_create(p);
    lv_obj_set_style_text_font(cl_wday, FONT_BIG, 0);
    lv_obj_set_style_text_color(cl_wday, lv_color_hex(0x8ab4f8), 0);
    lv_obj_align(cl_wday, LV_ALIGN_CENTER, 0, -110);

    cl_time = lv_label_create(p);
    lv_obj_set_style_text_font(cl_time, FONT_BIG, 0);
    lv_obj_set_style_text_color(cl_time, lv_color_hex(0xffffff), 0);
    lv_obj_align(cl_time, LV_ALIGN_CENTER, 0, -10);

    cl_date = lv_label_create(p);
    lv_obj_set_style_text_font(cl_date, FONT_BIG, 0);
    lv_obj_set_style_text_color(cl_date, lv_color_hex(0xc8d0e4), 0);
    lv_obj_align(cl_date, LV_ALIGN_CENTER, 0, 90);

    cl_last_min = -1;
    clock_update();
}

static const slide_t SLIDE_CLOCK = { "Uhrzeit & Datum", clock_build, clock_update };

// ========================= Slide 2: Netzwerk / IP ============================
// Aktualisiert nur bei Zustandswechsel (LVGL zeichnet gleichen Text ohnehin
// nicht neu) - keine dynamische Sekunden-Last.
static lv_obj_t *nw_mode, *nw_l1, *nw_l2;

static void network_update(void)
{
    if (!nw_mode) return;
    net_status_t st;
    network_manager_get_status(&st);

    if (st.mode == NET_MODE_STA_CONNECTED) {
        lv_label_set_text(nw_mode, "WLAN verbunden");
        lv_obj_set_style_text_color(nw_mode, lv_color_hex(0x7ce38b), 0);
        char l1[64], l2[48];
        snprintf(l1, sizeof(l1), "SSID: %s", st.ssid);
        snprintf(l2, sizeof(l2), "IP: %s", st.ip);
        lv_label_set_text(nw_l1, l1);
        lv_label_set_text(nw_l2, l2);
    } else if (st.mode == NET_MODE_INSTALLER_AP) {
        lv_label_set_text(nw_mode, "Einrichtung noetig");
        lv_obj_set_style_text_color(nw_mode, lv_color_hex(0xf5c542), 0);
        lv_label_set_text(nw_l1, "WLAN \"" NET_AP_SSID "\"  (PW: " NET_AP_PASS ")");
        lv_label_set_text(nw_l2, "Browser: http://" NET_AP_IP);
    } else {
        lv_label_set_text(nw_mode, "Verbinde mit WLAN ...");
        lv_obj_set_style_text_color(nw_mode, lv_color_hex(0x8ab4f8), 0);
        char l1[64];
        snprintf(l1, sizeof(l1), "SSID: %s", st.ssid[0] ? st.ssid : "-");
        lv_label_set_text(nw_l1, l1);
        lv_label_set_text(nw_l2, "");
    }
}

static void network_build(lv_obj_t *p)
{
    nw_mode = lv_label_create(p);
    lv_obj_set_style_text_font(nw_mode, FONT_BIG, 0);
    lv_obj_align(nw_mode, LV_ALIGN_CENTER, 0, -60);

    nw_l1 = lv_label_create(p);
    lv_obj_set_style_text_font(nw_l1, FONT_MED, 0);
    lv_obj_set_style_text_color(nw_l1, lv_color_hex(0xe0e0e0), 0);
    lv_obj_align(nw_l1, LV_ALIGN_CENTER, 0, 20);

    nw_l2 = lv_label_create(p);
    lv_obj_set_style_text_font(nw_l2, FONT_MED, 0);
    lv_obj_set_style_text_color(nw_l2, lv_color_hex(0xb0b8d0), 0);
    lv_obj_align(nw_l2, LV_ALIGN_CENTER, 0, 70);

    network_update();
}

static const slide_t SLIDE_NETWORK = { "Netzwerk", network_build, network_update };

// ========================== Slide 3: WLAN-Empfang ============================
// Zeigt den ueber die letzten ~20 s (= die 2 vorherigen Slides) gemittelten
// Empfang. STATISCH (kein update-Callback) -> keine Neuzeichnungen, kein Flackern.
static void wifi_build(lv_obj_t *p)
{
    net_status_t st;
    network_manager_get_status(&st);

    lv_obj_t *val = lv_label_create(p);
    lv_obj_set_style_text_font(val, FONT_BIG, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(0xffffff), 0);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *qual = lv_label_create(p);
    lv_obj_set_style_text_font(qual, FONT_BIG, 0);
    lv_obj_align(qual, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *note = lv_label_create(p);
    lv_obj_set_style_text_font(note, FONT_MED, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0x8a94b0), 0);
    lv_obj_align(note, LV_ALIGN_CENTER, 0, 110);

    if (!st.connected) {
        lv_label_set_text(val, "-- dBm");
        lv_label_set_text(qual, "nicht verbunden");
        lv_obj_set_style_text_color(qual, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(note, "");
        return;
    }

    int avg = network_manager_get_avg_rssi();
    if (avg == 0) avg = st.rssi;   // Fallback, falls noch keine Mittelung vorliegt

    char v[24]; snprintf(v, sizeof(v), "%d dBm", avg);
    lv_label_set_text(val, v);

    const char *q; uint32_t c;
    if (avg >= -55)      { q = "Ausgezeichnet"; c = 0x7ce38b; }
    else if (avg >= -65) { q = "Gut";           c = 0x9ade7b; }
    else if (avg >= -75) { q = "Mittel";        c = 0xf5c542; }
    else                 { q = "Schwach";       c = 0xef6b6b; }
    lv_label_set_text(qual, q);
    lv_obj_set_style_text_color(qual, lv_color_hex(c), 0);
    lv_label_set_text(note, "Mittelwert der letzten 20 s");
}

static const slide_t SLIDE_WIFI = { "WLAN-Empfang", wifi_build, NULL };

// ========================== Slide 4: Fritzbox / Internet =====================
// Externe IP + Auslastung (aktuell / max). Aktualisiert waehrend sichtbar (die
// Auslastung ist dynamisch); LVGL zeichnet gleichbleibende Werte nicht neu.
static lv_obj_t *fb_ip, *fb_down, *fb_up;

static void fritzbox_update(void)
{
    if (!fb_ip) return;
    fritzbox_data_t d;
    fritzbox_get(&d);

    if (!d.reachable) {
        lv_label_set_text(fb_ip, "Fritzbox nicht erreichbar");
        lv_obj_set_style_text_color(fb_ip, lv_color_hex(0xf5c542), 0);
        lv_label_set_text(fb_down, "");
        lv_label_set_text(fb_up, "");
        return;
    }

    char l[80];
    snprintf(l, sizeof(l), "IP: %s", d.external_ip[0] ? d.external_ip : "-");
    lv_label_set_text(fb_ip, l);
    lv_obj_set_style_text_color(fb_ip, lv_color_hex(0xffffff), 0);

    // Integer-Mathematik (kein Float-printf noetig):
    // aktuelle Rate: BYTE/s -> Zehntel-Mbit/s = Bps*8/100000
    // max. Linkrate: bit/s  -> Mbit/s        = bps/1000000
    uint32_t dn10 = (uint32_t)((uint64_t)d.down_rate_Bps * 8 / 100000);
    uint32_t up10 = (uint32_t)((uint64_t)d.up_rate_Bps   * 8 / 100000);
    uint32_t dnmax = d.down_max_bps / 1000000;
    uint32_t upmax = d.up_max_bps   / 1000000;

    snprintf(l, sizeof(l), "Down: %u.%u / %u Mbit/s", (unsigned)(dn10 / 10), (unsigned)(dn10 % 10), (unsigned)dnmax);
    lv_label_set_text(fb_down, l);
    snprintf(l, sizeof(l), "Up: %u.%u / %u Mbit/s", (unsigned)(up10 / 10), (unsigned)(up10 % 10), (unsigned)upmax);
    lv_label_set_text(fb_up, l);
}

static void fritzbox_build(lv_obj_t *p)
{
    fb_ip = lv_label_create(p);
    lv_obj_set_style_text_font(fb_ip, FONT_BIG, 0);
    lv_obj_align(fb_ip, LV_ALIGN_CENTER, 0, -70);

    fb_down = lv_label_create(p);
    lv_obj_set_style_text_font(fb_down, FONT_MED, 0);
    lv_obj_set_style_text_color(fb_down, lv_color_hex(0x7ce38b), 0);
    lv_obj_align(fb_down, LV_ALIGN_CENTER, 0, 10);

    fb_up = lv_label_create(p);
    lv_obj_set_style_text_font(fb_up, FONT_MED, 0);
    lv_obj_set_style_text_color(fb_up, lv_color_hex(0x8ab4f8), 0);
    lv_obj_align(fb_up, LV_ALIGN_CENTER, 0, 60);

    fritzbox_update();
}

static const slide_t SLIDE_FRITZBOX = { "Internet (Fritzbox)", fritzbox_build, fritzbox_update };

// ============================================================================
void slides_register_all(void)
{
    slideshow_add(&SLIDE_CLOCK);
    slideshow_add(&SLIDE_NETWORK);
    slideshow_add(&SLIDE_WIFI);
    slideshow_add(&SLIDE_FRITZBOX);
}
