#include "display.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_lvgl_port.h"

static const char *TAG = "infoscreen";

// ---- Farb-Testbild -----------------------------------------------------------
// Zyklus durch die Grundfarben (+ Weiss/Schwarz als Referenz), 10 s je Farbe.
// Der Text nennt die ERWARTETE Farbe: stimmt Hintergrund == Text, ist die
// RGB565-Reihenfolge korrekt. Weichen sie ab (z. B. "ROT" auf blauem Grund),
// muessen die Bytes getauscht werden (esp_lvgl_port .flags.swap_bytes).
typedef struct { const char *name; uint32_t rgb; bool dark_text; } color_entry_t;

static const color_entry_t COLORS[] = {
    { "ROT (RED)",       0xFF0000, false },
    { "GRUEN (GREEN)",   0x00FF00, true  },
    { "BLAU (BLUE)",     0x0000FF, false },
    { "WEISS (WHITE)",   0xFFFFFF, true  },
    { "SCHWARZ (BLACK)", 0x000000, false },
};
#define NUM_COLORS (sizeof(COLORS) / sizeof(COLORS[0]))

static lv_obj_t *s_screen;
static lv_obj_t *s_name_label;
static lv_obj_t *s_hex_label;
static int s_idx;

static void apply_color(void)
{
    const color_entry_t *c = &COLORS[s_idx];
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(c->rgb), 0);

    // Textfarbe kontrastierend zum Hintergrund
    lv_color_t txt = c->dark_text ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
    lv_obj_set_style_text_color(s_name_label, txt, 0);
    lv_obj_set_style_text_color(s_hex_label, txt, 0);

    lv_label_set_text(s_name_label, c->name);
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%06X", (unsigned)(c->rgb & 0xFFFFFF));
    lv_label_set_text(s_hex_label, buf);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    s_idx = (s_idx + 1) % NUM_COLORS;
    apply_color();
}

static void build_color_test(lv_display_t *disp)
{
    lvgl_port_lock(0);

    s_screen = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    s_name_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_name_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_name_label, LV_ALIGN_CENTER, 0, -30);

    s_hex_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_hex_label, &lv_font_montserrat_28, 0);
    lv_obj_align(s_hex_label, LV_ALIGN_CENTER, 0, 30);

    s_idx = 0;
    apply_color();

    // 10-Sekunden-Wechsel
    lv_timer_create(tick_cb, 10000, NULL);

    lvgl_port_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp-infoscreen — Farb-Testbild");
    ESP_LOGI(TAG, "PSRAM: %u Bytes", (unsigned)esp_psram_get_size());

    lv_display_t *disp = display_init();
    build_color_test(disp);

    ESP_LOGI(TAG, "Farbzyklus laeuft (10 s je Farbe)");
}
