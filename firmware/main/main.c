#include "display.h"

#include "esp_log.h"
#include "esp_psram.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "infoscreen";

// Einfache „Hello"-Startseite, damit der Display-Bring-up sichtbar ist.
static void build_hello_screen(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101830), 0);   // dunkelblau
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "esp-infoscreen");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Display-Bring-up OK  -  800x480 RGB");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8AB4F8), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 12);

    // Ecken-Marker, um Aufloesung/Ausrichtung visuell zu pruefen.
    lv_obj_t *tl = lv_label_create(scr);
    lv_label_set_text(tl, "TL");
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_t *br = lv_label_create(scr);
    lv_label_set_text(br, "BR");
    lv_obj_align(br, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    lvgl_port_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp-infoscreen startet");
    ESP_LOGI(TAG, "PSRAM: %u Bytes", (unsigned)esp_psram_get_size());

    lv_display_t *disp = display_init();
    build_hello_screen(disp);

    ESP_LOGI(TAG, "Startseite gezeichnet");
    // app_main darf zurueckkehren; LVGL laeuft im eigenen Port-Task weiter.
}
