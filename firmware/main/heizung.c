#include "heizung.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR

// ~1 KB -> ins PSRAM (internes RAM ist der Engpass)
static EXT_RAM_BSS_ATTR heizung_data_t s_data;
static SemaphoreHandle_t s_lock;

void heizung_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_data.age = -1;
    s_lock = xSemaphoreCreateMutex();
}

void heizung_get(heizung_data_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); out->age = -1; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}

void heizung_set(const heizung_data_t *d)
{
    if (!s_lock || !d) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = *d;
    xSemaphoreGive(s_lock);
}
