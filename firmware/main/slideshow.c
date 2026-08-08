#include "slideshow.h"
#include "fonts_de.h"

#include <stdio.h>
#include "esp_lvgl_port.h"

#define MAX_SLIDES 12

static const slide_t *s_slides[MAX_SLIDES];
static int s_count;
static int s_current;

static lv_obj_t *s_title;
static lv_obj_t *s_content;
static lv_obj_t *s_footer;

static void show_slide(int idx)
{
    if (s_count == 0) return;
    s_current = idx;
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

    lvgl_port_unlock();
}

void slideshow_add(const slide_t *slide)
{
    if (s_count < MAX_SLIDES) s_slides[s_count++] = slide;
}

void slideshow_start(uint32_t interval_ms)
{
    lvgl_port_lock(0);
    show_slide(0);
    lv_timer_create(switch_cb, interval_ms, NULL);
    lv_timer_create(tick_cb, 1000, NULL);
    lvgl_port_unlock();
}
