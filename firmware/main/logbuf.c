#include "logbuf.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"

#define LOGBUF_SIZE 16384   // ~16 KB Ringpuffer im PSRAM

static EXT_RAM_BSS_ATTR char s_ring[LOGBUF_SIZE];
static size_t s_head;
static bool   s_wrapped;
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_prev;   // vorheriger Log-Handler (serielle Ausgabe)

static void ring_append(const char *p, int len)
{
    if (!s_lock || len <= 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < len; i++) {
        s_ring[s_head++] = p[i];
        if (s_head >= LOGBUF_SIZE) { s_head = 0; s_wrapped = true; }
    }
    xSemaphoreGive(s_lock);
}

static int log_vprintf(const char *fmt, va_list args)
{
    va_list ap; va_copy(ap, args);
    char tmp[256];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);      // in den Ring
    if (n > 0) ring_append(tmp, n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1);
    int r = s_prev ? s_prev(fmt, ap) : vprintf(fmt, ap); // weiter an die serielle Ausgabe
    va_end(ap);
    return r;
}

void logbuf_init(void)
{
    if (s_lock) return;
    s_lock = xSemaphoreCreateMutex();
    s_head = 0; s_wrapped = false;
    s_prev = esp_log_set_vprintf(log_vprintf);
}

size_t logbuf_get(char *out, size_t out_len)
{
    if (!s_lock || out_len == 0) return 0;
    size_t n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_wrapped) {
        size_t first = LOGBUF_SIZE - s_head;             // aeltester Teil: s_head..Ende
        size_t c1 = first < out_len ? first : out_len;
        memcpy(out, s_ring + s_head, c1); n += c1;
        if (n < out_len && s_head > 0) {                 // dann Anfang..s_head
            size_t c2 = s_head < (out_len - n) ? s_head : (out_len - n);
            memcpy(out + n, s_ring, c2); n += c2;
        }
    } else {
        size_t c = s_head < out_len ? s_head : out_len;
        memcpy(out, s_ring, c); n = c;
    }
    xSemaphoreGive(s_lock);
    return n;
}
