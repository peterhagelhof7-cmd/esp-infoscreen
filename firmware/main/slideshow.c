#include "slideshow.h"
#include "fonts_de.h"

#include <stdio.h>
#include "esp_timer.h"
#include "esp_lvgl_port.h"

#define MAX_SLIDES 16

static const slide_t *s_slides[MAX_SLIDES];
static int s_count;
static int s_current;

static lv_obj_t *s_title;
static lv_obj_t *s_content;
static lv_obj_t *s_footer;
static lv_obj_t *s_countdown;      // Sekunden bis zum naechsten Slide (untere Ecke)
static uint32_t  s_interval_ms;    // Wechselintervall
static int64_t   s_slide_start_us; // Startzeitpunkt des aktuellen Slides
static int       s_last_rem = -1;  // zuletzt angezeigter Countdown (nur bei Aenderung neu zeichnen)

static void show_slide(int idx)
{
    if (s_count == 0) return;
    s_current = idx;
    s_slide_start_us = esp_timer_get_time();
    s_last_rem = -1;
    const slide_t *sl = s_slides[idx];

    lv_label_set_text(s_title, sl->title ? sl->title : "");
    lv_obj_clean(s_content);
    if (sl->build) sl->build(s_content);

    char foot[16];
    snprintf(foot, sizeof(foot), "%d / %d", idx + 1, s_count);
    lv_label_set_text(s_footer, foot);
}

static void switch_cb(lv_timer_t *t)
{
    (void)t;
    show_slide((s_current + 1) % s_count);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_count == 0) return;
    if (s_slides[s_current]->update) s_slides[s_current]->update();

    // Countdown bis zum naechsten Slide (nur bei Sekundenwechsel neu zeichnen).
    if (s_countdown && s_interval_ms > 0) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_slide_start_us) / 1000;
        int rem = (int)(((int64_t)s_interval_ms - elapsed_ms + 999) / 1000);
        if (rem < 0) rem = 0;
        if (rem != s_last_rem) {
            s_last_rem = rem;
            char b[16]; snprintf(b, sizeof(b), "%d s", rem);
            lv_label_set_text(s_countdown, b);
        }
    }
}

void slideshow_init(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1428), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Titelzeile oben
    s_title = lv_label_create(scr);
    lv_obj_set_style_text_font(s_title, &montserrat_de_28, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x8ab4f8), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 18);

    // Inhaltsbereich (transparent, ohne Rand/Scroll)
    s_content = lv_obj_create(scr);
    lv_obj_set_size(s_content, 800, 372);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    // Fusszeile (Seitenanzeige)
    s_footer = lv_label_create(scr);
    lv_obj_set_style_text_color(s_footer, lv_color_hex(0x556080), 0);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -14);

    // Countdown bis zum naechsten Slide (untere rechte Ecke)
    s_countdown = lv_label_create(scr);
    lv_obj_set_style_text_font(s_countdown, &montserrat_de_14, 0);
    lv_obj_set_style_text_color(s_countdown, lv_color_hex(0x556080), 0);
    lv_label_set_text(s_countdown, "");
    lv_obj_align(s_countdown, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    lvgl_port_unlock();
}

void slideshow_add(const slide_t *slide)
{
    if (s_count < MAX_SLIDES) s_slides[s_count++] = slide;
}

void slideshow_start(uint32_t interval_ms)
{
    lvgl_port_lock(0);
    s_interval_ms = interval_ms;
    show_slide(0);
    lv_timer_create(switch_cb, interval_ms, NULL);
    lv_timer_create(tick_cb, 1000, NULL);
    lvgl_port_unlock();
}
