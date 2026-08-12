// ============================================================================
// esp-infoscreen — Diagnose-/Inventur-Firmware
// ----------------------------------------------------------------------------
// Liest zur Laufzeit aus, was auf dem Board steckt, und gibt es sowohl
// menschenlesbar als auch als eine Zeile "INV {json}" ueber Serial aus (die der
// Web-Flasher einliest). Erkannt werden:
//   - Chip-Modell/-Revision, Cores, Features (WiFi/BT/BLE)
//   - Flash-Groesse, PSRAM-Groesse, freies internes RAM, MAC
//   - DHT22: Pin-Scan (GPIO18 + GPIO17 am P4), Nachweis ueber die Checksumme
//   - Touch-Controller: BEST-EFFORT-I2C-Scan der herausgefuehrten Header-Pins
//     (17/18). GPIO19/20 = nativer USB-Serial-JTAG und werden bewusst NICHT
//     gescannt (das wuerde die Serial-Ausgabe kappen).
// ============================================================================

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

// --- DHT22 (Bit-Bang, wie in der Hauptfirmware) -----------------------------
static int wait_lvl(int pin, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if (esp_timer_get_time() - start > timeout_us) return -1;
    }
    return (int)(esp_timer_get_time() - start);
}

static bool dht_read(int pin, float *t_out, float *h_out)
{
    uint8_t data[5] = { 0 };
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    portDISABLE_INTERRUPTS();
    bool ok = wait_lvl(pin, 0, 100) >= 0 && wait_lvl(pin, 1, 100) >= 0 && wait_lvl(pin, 0, 100) >= 0;
    for (int i = 0; ok && i < 40; i++) {
        if (wait_lvl(pin, 1, 80) < 0) { ok = false; break; }
        int high = wait_lvl(pin, 0, 100);
        if (high < 0) { ok = false; break; }
        if (high > 45) data[i / 8] |= (uint8_t)(1u << (7 - (i % 8)));
    }
    portENABLE_INTERRUPTS();

    if (!ok) return false;
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) return false;

    int16_t rh = (int16_t)((data[0] << 8) | data[1]);
    int16_t rt = (int16_t)((data[2] << 8) | data[3]);
    bool neg = rt & 0x8000; if (neg) rt &= 0x7fff;
    *h_out = rh / 10.0f;
    *t_out = (neg ? -1.0f : 1.0f) * (rt / 10.0f);
    return true;
}

// Sucht den DHT22 auf den Header-Pins. Liefert den Pin oder -1.
static int dht_scan(float *t, float *h)
{
    const int pins[] = { 18, 17 };   // P4: Data an IO18 (Standard), IO17 als Alternative
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        vTaskDelay(pdMS_TO_TICKS(60));
        for (int tries = 0; tries < 4; tries++) {
            if (dht_read(pins[i], t, h)) return pins[i];
            vTaskDelay(pdMS_TO_TICKS(400));
        }
    }
    return -1;
}

// --- I2C-Scan (best effort) -------------------------------------------------
// Scannt ein Pin-Paar auf ACKende Adressen. Liefert Anzahl (fuellt found[]).
static int i2c_scan_pair(int sda, int scl, uint8_t *found, int maxf)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,                     // beliebiger freier Port
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) return 0;
    int n = 0;
    for (int a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(bus, a, 30) == ESP_OK) {
            if (n < maxf) found[n] = (uint8_t)a;
            n++;
        }
    }
    i2c_del_master_bus(bus);
    return n;
}

// --- Ausgabe ----------------------------------------------------------------
static const char *chip_model_name(esp_chip_model_t m)
{
    switch (m) {
        case CHIP_ESP32:   return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C6: return "ESP32-C6";
        default:           return "unbekannt";
    }
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(400));

    // Chip
    esp_chip_info_t ci; esp_chip_info(&ci);
    const char *model = chip_model_name(ci.model);
    int rev_major = ci.revision / 100, rev_minor = ci.revision % 100;
    char feats[48] = "";
    if (ci.features & CHIP_FEATURE_WIFI_BGN) strcat(feats, "WiFi ");
    if (ci.features & CHIP_FEATURE_BT)       strcat(feats, "BT ");
    if (ci.features & CHIP_FEATURE_BLE)      strcat(feats, "BLE ");
    if (feats[0] == '\0') strcpy(feats, "-");

    // Flash
    uint32_t flash_b = 0; esp_flash_get_size(NULL, &flash_b);
    int flash_mb = (int)(flash_b / (1024 * 1024));

    // PSRAM
    size_t ps = 0;
#if CONFIG_SPIRAM
    ps = esp_psram_get_size();
#endif
    int psram_mb = (int)(ps / (1024 * 1024));

    // MAC + internes RAM
    uint8_t mac[6] = { 0 }; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    // DHT22
    float dt = 0, dh = 0;
    int dht_pin = dht_scan(&dt, &dh);
    // Auf ganze Zahlen runden -> kein %f noetig (robust + garantiert gueltiges JSON)
    int dht_t = (int)(dt + (dt >= 0 ? 0.5f : -0.5f));
    int dht_h = (int)(dh + 0.5f);

    // Touch (best effort): nur die freien Header-Pins scannen; 19/20 = USB.
    uint8_t found[16];
    int nf = i2c_scan_pair(17, 18, found, 16);
    if (nf == 0) nf = i2c_scan_pair(18, 17, found, 16);   // vertauschte Belegung mitprobieren
    const char *touch = "nicht gefunden";
    for (int i = 0; i < nf; i++)
        if (found[i] == 0x5D || found[i] == 0x14) touch = "GT911 (Touch)";

    // --- menschenlesbarer Report ---
    printf("\n==================== esp-infoscreen Diagnose ====================\n");
    printf("Chip:      %s  rev %d.%d,  %d Core(s),  Features: %s\n", model, rev_major, rev_minor, ci.cores, feats);
    printf("Flash:     %d MB\n", flash_mb);
    printf("PSRAM:     %d MB%s\n", psram_mb, ps ? "" : "  (nicht gefunden / anderer Typ)");
    printf("Int. RAM:  %u KB frei\n", (unsigned)(int_free / 1024));
    printf("MAC:       %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (dht_pin >= 0) printf("DHT22:     erkannt an GPIO%d  (%d C / %d %% rF)\n", dht_pin, dht_t, dht_h);
    else              printf("DHT22:     nicht erkannt  (geprueft: GPIO18, GPIO17)\n");
    printf("Touch:     %s  (I2C-Scan 17/18, best effort; GPIO19/20 = USB, nicht scanbar)\n", touch);
    printf("=================================================================\n");
    printf("Die Zeile 'INV {...}' wird alle 2 s wiederholt (fuer den Web-Flasher).\n\n");

    // --- Maschinen-Zeile fuer den Web-Flasher (alle 2 s wiederholt) ---
    char i2c_list[80]; size_t o = 0; i2c_list[0] = '\0';
    for (int i = 0; i < nf && o + 8 < sizeof(i2c_list); i++)
        o += snprintf(i2c_list + o, sizeof(i2c_list) - o, "%s\"0x%02x\"", i ? "," : "", found[i]);

    for (;;) {
        printf("INV {\"chip\":\"%s\",\"rev\":\"%d.%d\",\"cores\":%d,\"features\":\"%s\","
               "\"flash_mb\":%d,\"psram_mb\":%d,\"int_ram_free_kb\":%u,"
               "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
               "\"dht\":{\"found\":%s,\"gpio\":%d,\"temp_c\":%d,\"hum\":%d},"
               "\"i2c\":{\"found\":[%s],\"touch\":\"%s\",\"note\":\"best-effort 17/18; 19/20=USB\"}}\n",
               model, rev_major, rev_minor, ci.cores, feats,
               flash_mb, psram_mb, (unsigned)(int_free / 1024),
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
               dht_pin >= 0 ? "true" : "false", dht_pin, dht_t, dht_h,
               i2c_list, touch);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
