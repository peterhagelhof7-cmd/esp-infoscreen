#include "dht22.h"
#include "board_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "dht22";
#define POLL_INTERVAL_MS (15 * 1000)   // DHT22 verlangt min. 2 s zwischen Messungen

static dht22_data_t s_data;
static SemaphoreHandle_t s_lock;

// Wartet, bis die Leitung den angegebenen Pegel erreicht. Liefert die
// gewartete Zeit in us, oder -1 bei Ueberschreiten von timeout_us (Fehler/
// Sensor antwortet nicht -> Abbruch statt Endlosschleife).
static int wait_level(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(DHT22_PIN) != level) {
        if (esp_timer_get_time() - start > timeout_us) return -1;
    }
    return (int)(esp_timer_get_time() - start);
}

static bool read_once(float *temp_c, float *hum_pct)
{
    uint8_t data[5] = { 0 };

    // Start-Signal: Leitung >=1 ms auf Low ziehen, dann als Eingang (Pull-up)
    // dem Sensor die Antwort ueberlassen.
    gpio_set_direction(DHT22_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT22_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_direction(DHT22_PIN, GPIO_MODE_INPUT);

    // Kurzes Zeitfenster mit harter Timing-Anforderung (~5 ms) - Interrupts
    // waehrenddessen aus, sonst verfaelscht ein WLAN-/Timer-Interrupt die
    // Pulsbreiten-Messung. Bewusst kurz gehalten (kein RMT-Peripherie-Aufwand
    // fuer einen alle 15 s gelesenen, unkritischen Sensor).
    portDISABLE_INTERRUPTS();

    // Sensor-Antwort: 80 us Low, 80 us High, dann beginnen die 40 Datenbits.
    bool ok = wait_level(0, 100) >= 0 && wait_level(1, 100) >= 0 && wait_level(0, 100) >= 0;

    for (int i = 0; ok && i < 40; i++) {
        if (wait_level(1, 80) < 0) { ok = false; break; }
        int high_us = wait_level(0, 100);
        if (high_us < 0) { ok = false; break; }
        // ~26-28 us High = Bit "0", ~70 us High = Bit "1".
        if (high_us > 45) data[i / 8] |= (uint8_t)(1u << (7 - (i % 8)));
    }

    portENABLE_INTERRUPTS();

    if (!ok) return false;

    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) {
        ESP_LOGW(TAG, "Checksummenfehler");
        return false;
    }

    int16_t raw_h = (int16_t)((data[0] << 8) | data[1]);
    int16_t raw_t = (int16_t)((data[2] << 8) | data[3]);
    bool neg = raw_t & 0x8000;
    if (neg) raw_t &= 0x7fff;

    *hum_pct = raw_h / 10.0f;
    *temp_c  = (neg ? -1.0f : 1.0f) * (raw_t / 10.0f);
    return true;
}

static void poll_once(void)
{
    float t = 0, h = 0;
    bool ok = read_once(&t, &h);

    // Unplausible Werte (Fehl-Lesung ausserhalb des Sensorbereichs) verwerfen.
    if (ok && (h < 0.0f || h > 100.0f || t < -40.0f || t > 80.0f)) {
        ESP_LOGW(TAG, "unplausible Werte verworfen (%.1f C / %.1f %%)", t, h);
        ok = false;
    }
    if (!ok) {
        ESP_LOGW(TAG, "Lesevorgang fehlgeschlagen");
        return;   // letzten gueltigen Wert behalten statt zu loeschen
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.valid   = true;
    s_data.temp_c  = t;
    s_data.hum_pct = h;
    xSemaphoreGive(s_lock);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000));   // Anlaufzeit des Sensors nach Power-on
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void dht22_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << DHT22_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   // Notbehelf; extern 4,7-10 kOhm empfohlen
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    xTaskCreate(poll_task, "dht22", 3072, NULL, 3, NULL);
}

void dht22_get(dht22_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
