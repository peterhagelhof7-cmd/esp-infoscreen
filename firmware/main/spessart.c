#include "spessart.h"
#include "http_util.h"

#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "spessart";
#define SPESSART_URL "https://www.spessartwetter.de/webimg/custom.html"
#define POLL_INTERVAL_MS (30 * 60 * 1000)   // alle 30 min

static spessart_data_t s_data;
static SemaphoreHandle_t s_lock;

// Zahl direkt VOR einem Einheiten-Marker herausziehen (z.B. "19,8 <marker>").
// Die Seite ist ISO-8859-1 (Grad = Byte 0xB0). Rueckwaerts ueber Leerzeichen,
// dann Ziffern/Komma/Minus sammeln.
static bool number_before(const char *html, const char *marker, char *out, size_t out_len)
{
    const char *p = strstr(html, marker);
    if (!p) return false;
    const char *e = p;
    while (e > html && *(e - 1) == ' ') e--;             // Leerzeichen ueberspringen
    const char *s = e;
    while (s > html) {
        char ch = *(s - 1);
        if (isdigit((unsigned char)ch) || ch == ',' || ch == '.' || ch == '-') s--;
        else break;
    }
    if (s == e) return false;
    size_t n = (size_t)(e - s);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

static void poll_once(void)
{
    static char buf[20000];
    spessart_data_t d = { 0 };

    int n = http_get(SPESSART_URL, buf, sizeof(buf));
    if (n > 0) {
        char temp[16], wind[16];
        // Temperatur: erste Zahl vor Grad-C  ("\xb0C" in ISO-8859-1)
        bool t_ok = number_before(buf, "\xb0" "C", temp, sizeof(temp));
        // Wind: erste Zahl vor "km/h"
        bool w_ok = number_before(buf, "km/h", wind, sizeof(wind));
        if (t_ok || w_ok) {
            d.valid = true;
            if (t_ok) strncpy(d.temp, temp, sizeof(d.temp) - 1);
            if (w_ok) strncpy(d.wind, wind, sizeof(d.wind) - 1);
        } else {
            ESP_LOGW(TAG, "keine Werte gefunden (Layout geaendert?)");
        }
    } else {
        ESP_LOGW(TAG, "Abruf fehlgeschlagen");
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = d;
    xSemaphoreGive(s_lock);
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(8000));
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void spessart_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(poll_task, "spessart", 8192, NULL, 3, NULL);
}

void spessart_get(spessart_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
