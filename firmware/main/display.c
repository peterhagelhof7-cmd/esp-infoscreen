#include "display.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "display";
static lv_display_t *s_disp;   // gespeichertes Handle fuer display_set_rotation()

lv_display_t *display_init(void)
{
    // --- Hintergrundbeleuchtung erst mal aus (bis Panel initialisiert) ---
    gpio_config_t bk_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << DISP_PIN_BCKL,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    gpio_set_level(DISP_PIN_BCKL, 0);

    // --- RGB-Panel anlegen (verifizierte Pins/Timings aus board_config.h) ---
    ESP_LOGI(TAG, "Erzeuge RGB-Panel %dx%d", DISP_H_RES, DISP_V_RES);
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .data_width = 16,
        .num_fbs = 2,                              // Doppelpuffer in PSRAM
        .bounce_buffer_size_px = DISP_H_RES * 10,  // Bounce-Puffer (Tearing/DMA)
        .dma_burst_size = 64,                      // ersetzt sram/psram_trans_align (IDF >=5.3)
        .de_gpio_num = DISP_PIN_DE,
        .pclk_gpio_num = DISP_PIN_PCLK,
        .vsync_gpio_num = DISP_PIN_VSYNC,
        .hsync_gpio_num = DISP_PIN_HSYNC,
        .disp_gpio_num = DISP_PIN_DISP,
        // Datenreihenfolge: auf ECHTER HW verifiziert (2026-08-07) — mit der
        // Reihenfolge Blau,Gruen,Rot waren Rot und Blau vertauscht (Gruen ok).
        // Dieses Board/Panel erwartet die R-Pins auf den niedrigen Datenbits:
        // low->high = Rot, Gruen, Blau.
        .data_gpio_nums = {
            DISP_PIN_R0, DISP_PIN_R1, DISP_PIN_R2, DISP_PIN_R3, DISP_PIN_R4,
            DISP_PIN_G0, DISP_PIN_G1, DISP_PIN_G2, DISP_PIN_G3, DISP_PIN_G4, DISP_PIN_G5,
            DISP_PIN_B0, DISP_PIN_B1, DISP_PIN_B2, DISP_PIN_B3, DISP_PIN_B4,
        },
        .timings = {
            .pclk_hz = DISP_PCLK_HZ,
            .h_res = DISP_H_RES,
            .v_res = DISP_V_RES,
            .hsync_pulse_width = DISP_HSYNC_PULSE,
            .hsync_back_porch = DISP_HSYNC_BACK,
            .hsync_front_porch = DISP_HSYNC_FRONT,
            .vsync_pulse_width = DISP_VSYNC_PULSE,
            .vsync_back_porch = DISP_VSYNC_BACK,
            .vsync_front_porch = DISP_VSYNC_FRONT,
            .flags = {
                .hsync_idle_low = 1,
                .vsync_idle_low = 1,
                .de_idle_high = 0,
                .pclk_active_neg = 1,
                .pclk_idle_high = 0,
            },
        },
        .flags = {
            .fb_in_psram = 1,   // Framebuffer im PSRAM (800*480*2 = 768 KB je Puffer)
        },
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // --- LVGL-Port initialisieren ---
    ESP_LOGI(TAG, "Initialisiere LVGL-Port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = DISP_H_RES * DISP_V_RES,   // Vollbild (full_refresh)
        .double_buffer = true,
        .hres = DISP_H_RES,
        .vres = DISP_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_spiram = true,
            .full_refresh = true,   // fuer RGB-Panels mit 2 Framebuffern empfohlen
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,        // Bounce-Buffer-Modus
            .avoid_tearing = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);

    // --- Hintergrundbeleuchtung an ---
    gpio_set_level(DISP_PIN_BCKL, 1);
    ESP_LOGI(TAG, "Display bereit");
    s_disp = disp;
    return disp;
}

void display_set_rotation(bool rotated_180)
{
    if (s_disp) display_set_rotated_180(s_disp, rotated_180);
}

void display_set_rotated_180(lv_display_t *disp, bool rotated)
{
    // LVGL-Software-Rotation (fuer eine Slideshow mit seltenen Updates guenstig).
    lvgl_port_lock(0);
    lv_display_set_rotation(disp, rotated ? LV_DISPLAY_ROTATION_180
                                          : LV_DISPLAY_ROTATION_0);
    lvgl_port_unlock();
}
