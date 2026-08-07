#include "slides.h"
#include "slideshow.h"
#include "network_manager.h"
#include "time_sync.h"

#include <stdio.h>
#include <time.h>
#include "lvgl.h"

// ============================ Slide 1: Uhr / Datum ===========================
static lv_obj_t *cl_time, *cl_date;

static const char *WD[] = { "Sonntag", "Montag", "Dienstag", "Mittwoch",
                            "Donnerstag", "Freitag", "Samstag" };

static void clock_update(void)
{
    if (!cl_time) return;
    if (!time_sync_is_valid()) {
        lv_label_set_text(cl_time, "--:--:--");
        lv_label_set_text(cl_date, "Warte auf Zeitserver ...");
        return;
    }
    time_t now = 0; time(&now);
    struct tm tm; localtime_r(&now, &tm);

    char t[16];
    snprintf(t, sizeof(t), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    lv_label_set_text(cl_time, t);

    char d[48];
    snprintf(d, sizeof(d), "%s, %02d.%02d.%04d",
             WD[tm.tm_wday % 7], tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    lv_label_set_text(cl_date, d);
}

static void clock_build(lv_obj_t *p)
{
    cl_time = lv_label_create(p);
    lv_obj_set_style_text_font(cl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(cl_time, lv_color_hex(0xffffff), 0);
    lv_obj_align(cl_time, LV_ALIGN_CENTER, 0, -24);

    cl_date = lv_label_create(p);
    lv_obj_set_style_text_font(cl_date, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(cl_date, lv_color_hex(0xb0b8d0), 0);
    lv_obj_align(cl_date, LV_ALIGN_CENTER, 0, 34);

    clock_update();
}

static const slide_t SLIDE_CLOCK = { "Uhrzeit & Datum", clock_build, clock_update };

// ========================= Slide 2: Netzwerk / IP ============================
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
    lv_obj_set_style_text_font(nw_mode, &lv_font_montserrat_28, 0);
    lv_obj_align(nw_mode, LV_ALIGN_CENTER, 0, -40);

    nw_l1 = lv_label_create(p);
    lv_obj_set_style_text_font(nw_l1, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(nw_l1, lv_color_hex(0xe0e0e0), 0);
    lv_obj_align(nw_l1, LV_ALIGN_CENTER, 0, 6);

    nw_l2 = lv_label_create(p);
    lv_obj_set_style_text_font(nw_l2, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(nw_l2, lv_color_hex(0xb0b8d0), 0);
    lv_obj_align(nw_l2, LV_ALIGN_CENTER, 0, 48);

    network_update();
}

static const slide_t SLIDE_NETWORK = { "Netzwerk", network_build, network_update };

// ========================== Slide 3: WLAN-Empfang ============================
static lv_obj_t *wf_val, *wf_qual;

static void wifi_update(void)
{
    if (!wf_val) return;
    net_status_t st;
    network_manager_get_status(&st);

    if (!st.connected) {
        lv_label_set_text(wf_val, "-- dBm");
        lv_label_set_text(wf_qual, "nicht verbunden");
        lv_obj_set_style_text_color(wf_qual, lv_color_hex(0xb0b8d0), 0);
        return;
    }
    char v[16];
    snprintf(v, sizeof(v), "%d dBm", st.rssi);
    lv_label_set_text(wf_val, v);

    const char *q; uint32_t c;
    if (st.rssi >= -55)      { q = "Ausgezeichnet"; c = 0x7ce38b; }
    else if (st.rssi >= -65) { q = "Gut";           c = 0x9ade7b; }
    else if (st.rssi >= -75) { q = "Mittel";        c = 0xf5c542; }
    else                     { q = "Schwach";       c = 0xef6b6b; }
    lv_label_set_text(wf_qual, q);
    lv_obj_set_style_text_color(wf_qual, lv_color_hex(c), 0);
}

static void wifi_build(lv_obj_t *p)
{
    wf_val = lv_label_create(p);
    lv_obj_set_style_text_font(wf_val, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(wf_val, lv_color_hex(0xffffff), 0);
    lv_obj_align(wf_val, LV_ALIGN_CENTER, 0, -24);

    wf_qual = lv_label_create(p);
    lv_obj_set_style_text_font(wf_qual, &lv_font_montserrat_28, 0);
    lv_obj_align(wf_qual, LV_ALIGN_CENTER, 0, 34);

    wifi_update();
}

static const slide_t SLIDE_WIFI = { "WLAN-Empfang", wifi_build, wifi_update };

// ============================================================================
void slides_register_all(void)
{
    slideshow_add(&SLIDE_CLOCK);
    slideshow_add(&SLIDE_NETWORK);
    slideshow_add(&SLIDE_WIFI);
}
