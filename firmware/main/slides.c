#include "slides.h"
#include "slideshow.h"
#include "network_manager.h"
#include "time_sync.h"
#include "fritzbox.h"
#include "muell.h"
#include "feiertage.h"
#include "termine.h"
#include "dwd.h"
#include "nina.h"
#include "spessart.h"
#include "dht22.h"
#include "owm.h"
#include "planes.h"
#include "telegram.h"
#include "config_store.h"
#include "fonts_de.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "esp_attr.h"
#include "lvgl.h"

// Die eigenen Fonts (fonts_de.h) enthalten echte Umlaut-Glyphen -> keine
// Transliteration mehr noetig. de_ascii() kopiert nur noch UTF-8-sicher nach out
// (schneidet am Puffer-Ende keine mehrbyte-Sequenz mittendrin ab).
static void de_ascii(const char *in, char *out, size_t out_len)
{
    if (out_len == 0) return;
    size_t o = 0, last_start = 0;
    for (size_t i = 0; in[i] && o + 1 < out_len; i++) {
        if (((unsigned char)in[i] & 0xC0) != 0x80) last_start = o;   // Anfang eines Zeichens
        out[o++] = in[i];
    }
    // Wurde mitten in einem Zeichen abgeschnitten (naechstes Quellbyte ist ein
    // Folgebyte), das angefangene letzte Zeichen verwerfen.
    if (((unsigned char)in[o] & 0xC0) == 0x80) o = last_start;
    out[o] = '\0';
}

// Groesste eingebaute LVGL-Schrift = Montserrat 48. Fuer die "Helden"-Werte.
#define FONT_BIG   (&montserrat_de_48)
#define FONT_MED   (&montserrat_de_28)
#define FONT_SM    (&montserrat_de_14)

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

static const slide_t SLIDE_CLOCK = { "clock", "Uhrzeit & Datum", clock_build, clock_update };

// ===================== Slide: Wetter (OpenWeatherMap) ========================
// ---- Wetter-Icons: aus LVGL-Primitiven (Scheiben/Balken) gezeichnet ----------
// Keine externen Assets, keine lv_line-Pointer (Lebensdauer-Problem). Groessen/
// Positionen als Promille der Box (box=Kantenlaenge).
#define WI_SUN  0xffd23f
#define WI_MOON 0xd6def0
#define WI_CLL  0xe6ecf7
#define WI_CLG  0x9aa4c0
#define WI_RAIN 0x5b9bd5

static lv_obj_t *wi_disc(lv_obj_t *p, int d, uint32_t col, int dx, int dy)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(o, LV_ALIGN_CENTER, dx, dy);
    return o;
}
static lv_obj_t *wi_bar(lv_obj_t *p, int w, int h, int r, uint32_t col, int dx, int dy)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(o, LV_ALIGN_CENTER, dx, dy);
    return o;
}
static void wi_cloud(lv_obj_t *p, int s, uint32_t col, int dx, int dy)
{
    wi_bar(p, s * 66 / 100, s * 26 / 100, s * 13 / 100, col, dx, dy + s * 10 / 100);
    wi_disc(p, s * 34 / 100, col, dx - s * 18 / 100, dy + s * 2 / 100);
    wi_disc(p, s * 44 / 100, col, dx + s * 2 / 100,  dy - s * 6 / 100);
    wi_disc(p, s * 30 / 100, col, dx + s * 22 / 100, dy + s * 4 / 100);
}

// Zeichnet das zum OWM-Icon-Code (z.B. "10d") passende Symbol in einen neuen
// box*box-Container in p. Container wird zurueckgegeben (Aufrufer richtet aus).
static lv_obj_t *weather_icon(lv_obj_t *p, const char *code, int box)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, box, box);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    int s = box;
    int pfx = (code && code[0] && code[1]) ? (code[0] - '0') * 10 + (code[1] - '0') : 3;
    bool night = code && code[0] && code[2] == 'n';
    uint32_t disc = night ? WI_MOON : WI_SUN;

    switch (pfx) {
    case 1:   // klarer Himmel: Sonne/Mond (Sonne mit Strahlen)
        wi_disc(c, s * 52 / 100, disc, 0, 0);
        if (!night) {
            wi_bar(c, s * 8 / 100,  s * 20 / 100, s * 4 / 100, disc, 0, -s * 38 / 100);
            wi_bar(c, s * 8 / 100,  s * 20 / 100, s * 4 / 100, disc, 0,  s * 38 / 100);
            wi_bar(c, s * 20 / 100, s * 8 / 100,  s * 4 / 100, disc, -s * 38 / 100, 0);
            wi_bar(c, s * 20 / 100, s * 8 / 100,  s * 4 / 100, disc,  s * 38 / 100, 0);
        }
        break;
    case 2:   // wenige Wolken: Sonne/Mond + Wolke
        wi_disc(c, s * 36 / 100, disc, -s * 18 / 100, -s * 20 / 100);
        wi_cloud(c, s * 88 / 100, WI_CLL, s * 6 / 100, s * 12 / 100);
        break;
    case 3:   wi_cloud(c, s, WI_CLL, 0, -s * 4 / 100); break;
    case 4:   wi_cloud(c, s, WI_CLG, 0, -s * 4 / 100); break;
    case 9:
    case 10:  // Regen
        wi_cloud(c, s * 92 / 100, WI_CLL, 0, -s * 16 / 100);
        wi_bar(c, s * 7 / 100, s * 20 / 100, s * 3 / 100, WI_RAIN, -s * 18 / 100, s * 30 / 100);
        wi_bar(c, s * 7 / 100, s * 20 / 100, s * 3 / 100, WI_RAIN, 0,             s * 34 / 100);
        wi_bar(c, s * 7 / 100, s * 20 / 100, s * 3 / 100, WI_RAIN, s * 18 / 100,  s * 30 / 100);
        break;
    case 11:  // Gewitter: graue Wolke + gelber Blitz
        wi_cloud(c, s * 92 / 100, WI_CLG, 0, -s * 16 / 100);
        wi_bar(c, s * 9 / 100, s * 30 / 100, s * 2 / 100, WI_SUN, 0, s * 30 / 100);
        break;
    case 13:  // Schnee
        wi_cloud(c, s * 92 / 100, WI_CLL, 0, -s * 16 / 100);
        wi_disc(c, s * 10 / 100, 0xffffff, -s * 18 / 100, s * 30 / 100);
        wi_disc(c, s * 10 / 100, 0xffffff, 0,             s * 34 / 100);
        wi_disc(c, s * 10 / 100, 0xffffff, s * 18 / 100,  s * 30 / 100);
        break;
    case 50:  // Nebel
        wi_cloud(c, s * 80 / 100, WI_CLG, 0, -s * 22 / 100);
        wi_bar(c, s * 64 / 100, s * 7 / 100, s * 3 / 100, WI_CLG, 0, s * 20 / 100);
        wi_bar(c, s * 54 / 100, s * 7 / 100, s * 3 / 100, WI_CLG, 0, s * 36 / 100);
        break;
    default:  wi_cloud(c, s, WI_CLL, 0, -s * 4 / 100); break;
    }
    return c;
}

// Obere Haelfte: aktuelle Werte. Untere Haelfte: 3 gleich grosse Zellen
// (Morgen / Uebermorgen / Wochentag danach) mit Min/Max + Kurz-Wetter.
static void owm_build(lv_obj_t *p)
{
    owm_data_t d; owm_get(&d);

    if (!d.has_key || !d.valid) {
        lv_obj_t *l = lv_label_create(p);
        lv_obj_set_style_text_font(l, FONT_MED, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(l, !d.has_key ? "Kein API-Key (Einstellungen)" : "Wetter nicht verf\xc3\xbcgbar");
        lv_obj_center(l);
        return;
    }

    // --- obere Haelfte: aktuell ---
    char temp[24]; snprintf(temp, sizeof(temp), "%d \xc2\xb0""C", d.temp);
    lv_obj_t *lt = lv_label_create(p);
    lv_obj_set_style_text_font(lt, FONT_BIG, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0xffffff), 0);
    lv_label_set_text(lt, temp);
    lv_obj_align(lt, LV_ALIGN_TOP_MID, 0, 6);

    char desc[64]; de_ascii(d.desc, desc, sizeof(desc));
    if (desc[0]) desc[0] = (char)toupper((unsigned char)desc[0]);
    lv_obj_t *ld = lv_label_create(p);
    lv_obj_set_style_text_font(ld, FONT_MED, 0);
    lv_obj_set_style_text_color(ld, lv_color_hex(0x8ab4f8), 0);
    lv_label_set_text(ld, desc);
    lv_obj_align(ld, LV_ALIGN_TOP_MID, 0, 66);

    char more[80];
    snprintf(more, sizeof(more), "gef\xc3\xbchlt %d \xc2\xb0""C  -  %d%% rF  -  Wind %d km/h", d.feels, d.humidity, d.wind_kmh);
    lv_obj_t *lm = lv_label_create(p);
    lv_obj_set_style_text_font(lm, FONT_MED, 0);
    lv_obj_set_style_text_color(lm, lv_color_hex(0xb0b8d0), 0);
    lv_label_set_text(lm, more);
    lv_obj_align(lm, LV_ALIGN_TOP_MID, 0, 112);

    if (d.icon[0]) {
        lv_obj_t *ic = weather_icon(p, d.icon, 104);
        lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 44, 18);
    }

    // --- untere Haelfte: 3 Vorhersage-Zellen ---
    if (!d.fc_valid) return;
    const int cx[3] = { -258, 0, 258 };   // Zellenmittelpunkte relativ zur Mitte
    for (int k = 0; k < 3; k++) {
        if (!d.fc[k].valid) continue;
        lv_obj_t *cell = lv_obj_create(p);
        lv_obj_set_size(cell, 244, 178);
        lv_obj_align(cell, LV_ALIGN_BOTTOM_MID, cx[k], -4);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x16203c), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x33406a), 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_set_style_pad_all(cell, 6, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *hdr = lv_label_create(cell);
        lv_obj_set_style_text_font(hdr, FONT_MED, 0);
        lv_obj_set_style_text_color(hdr, lv_color_hex(0x8ab4f8), 0);
        lv_label_set_text(hdr, d.fc[k].label);
        lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

        if (d.fc[k].icon[0]) {
            lv_obj_t *ic = weather_icon(cell, d.fc[k].icon, 50);
            lv_obj_align(ic, LV_ALIGN_CENTER, 0, -22);
        }

        char tt[24]; snprintf(tt, sizeof(tt), "%d\xc2\xb0 / %d\xc2\xb0", d.fc[k].tmin, d.fc[k].tmax);
        lv_obj_t *lte = lv_label_create(cell);
        lv_obj_set_style_text_font(lte, FONT_MED, 0);
        lv_obj_set_style_text_color(lte, lv_color_hex(0xffffff), 0);
        lv_label_set_text(lte, tt);
        lv_obj_align(lte, LV_ALIGN_CENTER, 0, 32);

        char cd[40]; de_ascii(d.fc[k].desc, cd, sizeof(cd));
        if (cd[0] >= 'a' && cd[0] <= 'z') cd[0] = (char)(cd[0] - 'a' + 'A');   // nur ASCII gross
        lv_obj_t *lcd = lv_label_create(cell);
        lv_obj_set_style_text_font(lcd, FONT_SM, 0);
        lv_obj_set_style_text_color(lcd, lv_color_hex(0xb0b8d0), 0);
        lv_obj_set_width(lcd, 228);
        lv_obj_set_style_text_align(lcd, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lcd, LV_LABEL_LONG_WRAP);
        lv_label_set_text(lcd, cd);
        lv_obj_align(lcd, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static const slide_t SLIDE_OWM = { "owm", "Wetter Johannesberg", owm_build, NULL };

// ========================= Slide: Flugzeuge ==================================
// Naechste Flugzeuge ueber dem Standort (airplanes.live). Kopfzeile + Liste der
// bis zu 6 naechsten Maschinen: Callsign, Typ, Hoehe, Entfernung, Kurs.
static void alt_text(int alt_ft, char *out, size_t len)
{
    if (alt_ft < 0)            snprintf(out, len, "Boden");
    else if (alt_ft >= 18000)  snprintf(out, len, "FL%03d", alt_ft / 100);
    else                       snprintf(out, len, "%d ft", alt_ft);
}

static const char *compass(int track)
{
    if (track < 0) return "";
    static const char *DIR[] = { "N", "NO", "O", "SO", "S", "SW", "W", "NW" };
    return DIR[((track + 22) / 45) % 8];
}

static void planes_build(lv_obj_t *p)
{
    planes_data_t d; planes_get(&d);

    lv_obj_t *hdr = lv_label_create(p);
    lv_obj_set_style_text_font(hdr, FONT_MED, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x8ab4f8), 0);
    char htxt[48];
    if (!d.valid)      snprintf(htxt, sizeof(htxt), "\xE2\x9C\x88 Flugzeuge \xE2\x80\x93 kein Abruf");
    else               snprintf(htxt, sizeof(htxt), "\xE2\x9C\x88 Flugzeuge in der N\xC3\xA4he: %d", d.count);
    lv_label_set_text(hdr, htxt);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 24, 12);

    if (!d.valid || d.count == 0) {
        lv_obj_t *l = lv_label_create(p);
        lv_obj_set_style_text_font(l, FONT_MED, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(l, d.valid ? "Zurzeit keine Flugzeuge in der N\xC3\xA4he"
                                     : "Daten noch nicht verf\xC3\xBcgbar");
        lv_obj_center(l);
        return;
    }

    int rows = d.count < 6 ? d.count : 6;   // bis zu 6 Zeilen zeigen
    for (int i = 0; i < rows; i++) {
        const plane_t *a = &d.ac[i];
        lv_obj_t *row = lv_obj_create(p);
        lv_obj_set_size(row, 752, 56);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 70 + i * 64);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x16203c), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x33406a), 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // Links: Callsign (oben) + Route bzw. Typ (unten).
        // Hinweis: Font hat KEIN "\xE2\x86\x92"/"\xC2\xB7" -> auf dem Display ">" und Leerzeichen.
        const char *cs = a->flight[0] ? a->flight : (a->reg[0] ? a->reg : "?");
        lv_obj_t *lcs = lv_label_create(row);
        lv_obj_set_style_text_font(lcs, FONT_MED, 0);
        lv_obj_set_style_text_color(lcs, lv_color_hex(0xffffff), 0);
        lv_label_set_text(lcs, cs);
        lv_obj_align(lcs, LV_ALIGN_TOP_LEFT, 0, 0);

        char sub[32];
        if (a->from[0] || a->to[0])
            snprintf(sub, sizeof(sub), "%s > %s", a->from[0] ? a->from : "?", a->to[0] ? a->to : "?");
        else if (a->type[0])
            snprintf(sub, sizeof(sub), "%s", a->type);
        else
            sub[0] = '\0';
        lv_obj_t *lsub = lv_label_create(row);
        lv_obj_set_style_text_font(lsub, FONT_SM, 0);
        lv_obj_set_style_text_color(lsub, lv_color_hex(0x8ab4f8), 0);
        lv_label_set_text(lsub, sub);
        lv_obj_align(lsub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        // Rechts: Typ, Hoehe, Entfernung, Kurs (Leerzeichen als Trenner).
        char alt[12]; alt_text(a->alt_ft, alt, sizeof(alt));
        char info[72];
        size_t io = 0;
        if (a->type[0]) io += snprintf(info + io, sizeof(info) - io, "%s   ", a->type);
        io += snprintf(info + io, sizeof(info) - io, "%s   %.0f nm", alt, a->dst_nm);
        if (a->track >= 0)
            io += snprintf(info + io, sizeof(info) - io, "   %d\xC2\xB0 %s", a->track, compass(a->track));
        lv_obj_t *li = lv_label_create(row);
        lv_obj_set_style_text_font(li, FONT_MED, 0);
        lv_obj_set_style_text_color(li, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(li, info);
        lv_obj_align(li, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static const slide_t SLIDE_PLANES = { "planes", "Flugzeuge", planes_build, NULL };

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
        lv_label_set_text(nw_mode, "Einrichtung n\xc3\xb6tig");
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

static const slide_t SLIDE_NETWORK = { "net", "Netzwerk", network_build, network_update };

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

static const slide_t SLIDE_WIFI = { "wifi", "WLAN-Empfang", wifi_build, NULL };

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

static const slide_t SLIDE_FRITZBOX = { "fritz", "Internet (Fritzbox)", fritzbox_build, fritzbox_update };

// ===================== Slide 5: Termine / Kalender ===========================
// Naechste 5 Ereignisse = Nutzer-Termine + Muellabfuhr, nach Datum sortiert.
// Statisch (baut bei jedem Anzeigen neu) -> kein Flackern.
typedef struct { char date[11]; char label[56]; } cal_event_t;

static void calendar_build(lv_obj_t *p)
{
    char today[11]; time_today_str(today, sizeof(today));

    // Statisch (laeuft nur im LVGL-Task, nicht reentrant) -> spart Stack.
    // EXT_RAM_BSS_ATTR: im PSRAM, entlastet den knappen internen RAM.
    static EXT_RAM_BSS_ATTR cal_event_t ev[100];
    static EXT_RAM_BSS_ATTR termine_entry_t te[50];
    static EXT_RAM_BSS_ATTR muell_entry_t me[48];
    int n = 0;

    // Nutzer-Termine
    int tn = termine_get_all(te, 50);
    for (int i = 0; i < tn && n < 100; i++) {
        if (today[0] && strcmp(te[i].date, today) < 0) continue;   // Vergangenes weglassen
        strncpy(ev[n].date, te[i].date, sizeof(ev[0].date) - 1); ev[n].date[10] = '\0';
        char ti[48]; de_ascii(te[i].title, ti, sizeof(ti));
        if (te[i].time[0]) snprintf(ev[n].label, sizeof(ev[n].label), "%s %s", te[i].time, ti);
        else              snprintf(ev[n].label, sizeof(ev[n].label), "%s", ti);
        n++;
    }
    // Muellabfuhr
    int mn = muell_get(me, 48);
    for (int i = 0; i < mn && n < 100; i++) {
        strncpy(ev[n].date, me[i].day, sizeof(ev[0].date) - 1); ev[n].date[10] = '\0';
        char ti[32]; de_ascii(me[i].title, ti, sizeof(ti));
        snprintf(ev[n].label, sizeof(ev[n].label), "Abfuhr: %s", ti);
        n++;
    }
    // Feiertage (gewaehltes Bundesland)
    static EXT_RAM_BSS_ATTR feiertag_t fe[40];
    int fn = feiertage_get(fe, 40);
    for (int i = 0; i < fn && n < 100; i++) {
        if (today[0] && strcmp(fe[i].date, today) < 0) continue;
        strncpy(ev[n].date, fe[i].date, sizeof(ev[0].date) - 1); ev[n].date[10] = '\0';
        snprintf(ev[n].label, sizeof(ev[n].label), "%s", fe[i].name);
        n++;
    }
    // nach Datum sortieren (ISO-String)
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(ev[j].date, ev[i].date) < 0) { cal_event_t t = ev[i]; ev[i] = ev[j]; ev[j] = t; }

    int show = n < 5 ? n : 5;
    if (show == 0) {
        lv_obj_t *l = lv_label_create(p);
        lv_obj_set_style_text_font(l, FONT_MED, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(l, "Keine Termine");
        lv_obj_center(l);
        return;
    }

    // Rahmen gleichmaessig ueber die Inhaltsflaeche (800x372) verteilen, gleiche Hoehe.
    const int area_h = 372, margin = 4, gap = 6, width = 760;
    int frame_h = (area_h - 2 * margin - (show - 1) * gap) / show;
    for (int i = 0; i < show; i++) {
        int yy = 0, mm = 0, dd = 0;
        sscanf(ev[i].date, "%d-%d-%d", &yy, &mm, &dd);
        char line[80];
        snprintf(line, sizeof(line), "%02d.%02d.   %s", dd, mm, ev[i].label);

        lv_obj_t *box = lv_obj_create(p);
        lv_obj_set_size(box, width, frame_h);
        lv_obj_align(box, LV_ALIGN_TOP_MID, 0, margin + i * (frame_h + gap));
        lv_obj_set_style_bg_color(box, lv_color_hex(0x16203c), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(0x33406a), 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 8, 0);
        lv_obj_set_style_pad_left(box, 16, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *l = lv_label_create(box);
        lv_obj_set_style_text_font(l, FONT_MED, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xe6e6e6), 0);
        lv_obj_set_width(l, width - 40);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_label_set_text(l, line);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static const slide_t SLIDE_CALENDAR = { "cal", "Termine", calendar_build, NULL };

// =================== Slide 6: Warnungen (BBK/NINA + DWD) =====================
// Zeigt amtliche Katastrophen-/Bevoelkerungsschutz-Warnungen (BBK/NINA: MoWaS,
// Hochwasser, KATWARN ...) und, falls dort nichts, DWD-Wetterwarnungen.
static void dwd_build(lv_obj_t *p)
{
    nina_data_t nn; nina_get(&nn);
    dwd_data_t  d;  dwd_get(&d);

    lv_obj_t *l1 = lv_label_create(p);
    lv_obj_set_style_text_font(l1, FONT_BIG, 0);
    lv_obj_set_width(l1, 740);
    lv_label_set_long_mode(l1, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);

    bool have_nina = nn.valid && nn.count > 0 && nn.headline[0];
    bool have_dwd  = d.valid  && d.count  > 0 && d.headline[0];

    if (!have_nina && !have_dwd) {
        bool any = nn.valid || d.valid;
        lv_label_set_text(l1, any ? "Keine Warnung" : "Warnquellen nicht erreichbar");
        lv_obj_set_style_text_color(l1, lv_color_hex(any ? 0x7ce38b : 0xb0b8d0), 0);
        lv_obj_center(l1);
        return;
    }

    // NINA hat Vorrang (amtlich, umfassend); sonst DWD.
    const char *raw_hl, *sev, *src; int cnt;
    if (have_nina) { raw_hl = nn.headline; sev = nn.severity; src = nn.provider[0] ? nn.provider : "BBK"; cnt = nn.count; }
    else           { raw_hl = d.headline;  sev = d.severity;  src = "DWD";  cnt = d.count; }

    char hl[96]; de_ascii(raw_hl, hl, sizeof(hl));
    lv_label_set_text(l1, hl[0] ? hl : "Warnung aktiv");
    uint32_t c = 0xf5c542;   // gelb (Minor/Moderate)
    if (strcmp(sev, "Severe") == 0 || strcmp(sev, "Extreme") == 0) c = 0xef6b6b;  // rot
    lv_obj_set_style_text_color(l1, lv_color_hex(c), 0);
    lv_obj_align(l1, LV_ALIGN_CENTER, 0, -20);

    // Fusszeile: Quelle + Anzahl
    char sub[48];
    if (cnt > 1) snprintf(sub, sizeof(sub), "Quelle: %s  -  %d Warnungen", src, cnt);
    else         snprintf(sub, sizeof(sub), "Quelle: %s", src);
    lv_obj_t *l2 = lv_label_create(p);
    lv_obj_set_style_text_font(l2, FONT_MED, 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x8ab4f8), 0);
    lv_label_set_text(l2, sub);
    lv_obj_align(l2, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static const slide_t SLIDE_DWD = { "dwd", "Warnungen", dwd_build, NULL };

// ==================== Slide 7: Spessartwetter (Temp/Wind) ====================
static void spessart_build(lv_obj_t *p)
{
    spessart_data_t s; spessart_get(&s);

    if (!s.valid) {
        lv_obj_t *l = lv_label_create(p);
        lv_obj_set_style_text_font(l, FONT_MED, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(l, "spessartwetter nicht erreichbar");
        lv_obj_center(l);
        return;
    }

    char t[24]; snprintf(t, sizeof(t), "%s \xc2\xb0""C", s.temp[0] ? s.temp : "?");
    lv_obj_t *lt = lv_label_create(p);
    lv_obj_set_style_text_font(lt, FONT_BIG, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0xffffff), 0);
    lv_label_set_text(lt, t);
    lv_obj_align(lt, LV_ALIGN_CENTER, 0, -70);

    char w[32]; snprintf(w, sizeof(w), "Wind: %s km/h", s.wind[0] ? s.wind : "?");
    lv_obj_t *lw = lv_label_create(p);
    lv_obj_set_style_text_font(lw, FONT_MED, 0);
    lv_obj_set_style_text_color(lw, lv_color_hex(0x8ab4f8), 0);
    lv_label_set_text(lw, w);
    lv_obj_align(lw, LV_ALIGN_CENTER, 0, 5);

    // Windboeen; ab 50 km/h rot hervorheben.
    char b[32]; snprintf(b, sizeof(b), "B\xc3\xb6" "en: %s km/h", s.gust[0] ? s.gust : "?");
    lv_obj_t *lb = lv_label_create(p);
    lv_obj_set_style_text_font(lb, FONT_MED, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(spessart_gust_kmh(&s) >= 50.0 ? 0xef6b6b : 0xf5a742), 0);
    lv_label_set_text(lb, b);
    lv_obj_align(lb, LV_ALIGN_CENTER, 0, 60);
}

static const slide_t SLIDE_SPESSART = { "spessart", "Spessartwetter", spessart_build, NULL };

// ================= Slide 8: Innenraum (DHT22 an P4/GPIO18) ===================
static void innenraum_build(lv_obj_t *p)
{
    dht22_data_t d; dht22_get(&d);

    if (!d.valid) {
        lv_obj_t *l = lv_label_create(p);
        lv_obj_set_style_text_font(l, FONT_MED, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xb0b8d0), 0);
        lv_label_set_text(l, "Sensor nicht erreichbar");
        lv_obj_center(l);
        return;
    }

    char t[24]; snprintf(t, sizeof(t), "%.1f \xc2\xb0""C", d.temp_c);
    lv_obj_t *lt = lv_label_create(p);
    lv_obj_set_style_text_font(lt, FONT_BIG, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0xffffff), 0);
    lv_label_set_text(lt, t);
    lv_obj_align(lt, LV_ALIGN_CENTER, 0, -40);

    char h[24]; snprintf(h, sizeof(h), "%.0f %% Luftfeuchte", d.hum_pct);
    lv_obj_t *lh = lv_label_create(p);
    lv_obj_set_style_text_font(lh, FONT_MED, 0);
    lv_obj_set_style_text_color(lh, lv_color_hex(0x8ab4f8), 0);
    lv_label_set_text(lh, h);
    lv_obj_align(lh, LV_ALIGN_CENTER, 0, 45);
}

static const slide_t SLIDE_INNENRAUM = { "innen", "Innenraum", innenraum_build, NULL };

// ==================== Slide 9: Message Board (Telegram) =====================
// Zeigt den Chatverlauf der konfigurierten Telegram-Gruppe. Wird nur neu
// gezeichnet, wenn sich der Verlauf geaendert hat (Versionszaehler).
static lv_obj_t *mb_label;
static unsigned mb_last_ver = (unsigned)-1;

static void msgboard_render(void)
{
    if (!mb_label) return;
    static EXT_RAM_BSS_ATTR tg_msg_t msgs[TG_MAX_MSGS];   // PSRAM, LVGL-Task (nicht reentrant)
    int n = telegram_get_history(msgs, TG_MAX_MSGS);

    if (!telegram_configured()) {
        lv_label_set_text(mb_label, "Telegram nicht konfiguriert.\n"
                                    "Token und Gruppen-ID im Webinterface hinterlegen.");
        lv_obj_set_style_text_color(mb_label, lv_color_hex(0xb0b8d0), 0);
        return;
    }
    if (n == 0) {
        lv_label_set_text(mb_label, "Noch keine Nachrichten.");
        lv_obj_set_style_text_color(mb_label, lv_color_hex(0xb0b8d0), 0);
        return;
    }

    static EXT_RAM_BSS_ATTR char text[2600];   // PSRAM
    int start = n > 12 ? n - 12 : 0;   // die letzten bis zu 12 Zeilen
    size_t o = 0;
    for (int i = start; i < n && o + 4 < sizeof(text); i++) {
        char fr[24], tx[200];
        de_ascii(msgs[i].from, fr, sizeof(fr));
        de_ascii(msgs[i].text, tx, sizeof(tx));
        o += snprintf(text + o, sizeof(text) - o, "%s%s: %s", i > start ? "\n" : "", fr, tx);
    }
    lv_obj_set_style_text_color(mb_label, lv_color_hex(0xe6e6e6), 0);
    lv_label_set_text(mb_label, text);
}

static void msgboard_update(void)
{
    unsigned v = telegram_history_version();
    if (v == mb_last_ver) return;   // nichts Neues -> nicht neu zeichnen
    mb_last_ver = v;
    msgboard_render();
}

static void msgboard_build(lv_obj_t *p)
{
    mb_label = lv_label_create(p);
    lv_obj_set_style_text_font(mb_label, FONT_SM, 0);
    lv_obj_set_width(mb_label, 760);
    lv_label_set_long_mode(mb_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(mb_label, LV_ALIGN_TOP_LEFT, 16, 4);
    mb_last_ver = (unsigned)-1;      // beim naechsten update erzwingen
    msgboard_update();
}

static const slide_t SLIDE_MSGBOARD = { "msg", "Message Board", msgboard_build, msgboard_update };

// ============================================================================
// Katalog aller Slides (Reihenfolge = Anzeigereihenfolge).
static const slide_t *ALL_SLIDES[] = {
    &SLIDE_CLOCK, &SLIDE_OWM, &SLIDE_PLANES, &SLIDE_CALENDAR, &SLIDE_DWD,
    &SLIDE_SPESSART, &SLIDE_INNENRAUM, &SLIDE_FRITZBOX, &SLIDE_NETWORK, &SLIDE_WIFI,
    &SLIDE_MSGBOARD,
};
#define N_SLIDES (int)(sizeof(ALL_SLIDES) / sizeof(ALL_SLIDES[0]))

// Slide aktiviert? Config-Schluessel "sl_<id>", Default an ("1").
static bool slide_on(const char *id)
{
    char key[16]; snprintf(key, sizeof(key), "sl_%s", id);
    char v[4]; config_get_str_def(key, v, sizeof(v), "1");
    return v[0] != '0';
}

void slides_register_all(void)
{
    int added = 0;
    for (int i = 0; i < N_SLIDES; i++) {
        if (slide_on(ALL_SLIDES[i]->id)) { slideshow_add(ALL_SLIDES[i]); added++; }
    }
    if (added == 0) slideshow_add(&SLIDE_CLOCK);   // niemals leer
}

int         slides_catalog_count(void)        { return N_SLIDES; }
const char *slides_catalog_id(int i)          { return (i >= 0 && i < N_SLIDES) ? ALL_SLIDES[i]->id : ""; }
const char *slides_catalog_title(int i)       { return (i >= 0 && i < N_SLIDES) ? ALL_SLIDES[i]->title : ""; }
bool        slides_catalog_enabled(int i)     { return (i >= 0 && i < N_SLIDES) ? slide_on(ALL_SLIDES[i]->id) : false; }
