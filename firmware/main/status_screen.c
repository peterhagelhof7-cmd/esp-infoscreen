#include "status_screen.h"
#include "network_manager.h"

#include <stdio.h>
#include "esp_lvgl_port.h"

static lv_obj_t *s_title;
static lv_obj_t *s_mode;
static lv_obj_t *s_line1;
static lv_obj_t *s_line2;

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    net_status_t st;
    network_manager_get_status(&st);

    if (st.mode == NET_MODE_STA_CONNECTED) {
        lv_label_set_text(s_mode, "WLAN verbunden");
        lv_obj_set_style_text_color(s_mode, lv_color_hex(0x7CE38B), 0);
        char l1[64], l2[48];
        snprintf(l1, sizeof(l1), "SSID: %s", st.ssid);
        snprintf(l2, sizeof(l2), "IP: %s   Empfang: %d dBm", st.ip, st.rssi);
        lv_label_set_text(s_line1, l1);
        lv_label_set_text(s_line2, l2);
    } else if (st.mode == NET_MODE_INSTALLER_AP) {
        lv_label_set_text(s_mode, "Einrichtung noetig");
        lv_obj_set_style_text_color(s_mode, lv_color_hex(0xF5C542), 0);
        lv_label_set_text(s_line1, "WLAN \"" NET_AP_SSID "\" verbinden (Passwort: " NET_AP_PASS ")");
        lv_label_set_text(s_line2, "Browser: http://" NET_AP_IP);
    } else {
        lv_label_set_text(s_mode, "Verbinde mit WLAN ...");
        lv_obj_set_style_text_color(s_mode, lv_color_hex(0x8AB4F8), 0);
        char l1[64];
        snprintf(l1, sizeof(l1), "SSID: %s", st.ssid[0] ? st.ssid : "-");
        lv_label_set_text(s_line1, l1);
        lv_label_set_text(s_line2, "");
    }
}

void status_screen_create(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1428), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "esp-infoscreen");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 60);

    s_mode = lv_label_create(scr);
    lv_obj_set_style_text_font(s_mode, &lv_font_montserrat_28, 0);
    lv_obj_align(s_mode, LV_ALIGN_CENTER, 0, -20);

    s_line1 = lv_label_create(scr);
    lv_obj_set_style_text_font(s_line1, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_line1, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(s_line1, LV_ALIGN_CENTER, 0, 30);

    s_line2 = lv_label_create(scr);
    lv_obj_set_style_text_font(s_line2, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_line2, lv_color_hex(0xB0B8D0), 0);
    lv_obj_align(s_line2, LV_ALIGN_CENTER, 0, 74);

    refresh_cb(NULL);
    lv_timer_create(refresh_cb, 2000, NULL);   // alle 2 s aktualisieren

    lvgl_port_unlock();
}
