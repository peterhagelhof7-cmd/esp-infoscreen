#include "http_util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "http";

// Serialisiert alle HTTP(S)-Abrufe: nur EINE (TLS-)Verbindung gleichzeitig.
// Sonst allozieren mehrere parallele Poller je einen TLS-Kontext und sprengen
// den internen RAM -> mbedtls_ssl_setup schlaegt fehl (-0x008D).
static SemaphoreHandle_t s_gate;

void http_util_init(void)
{
    if (!s_gate) s_gate = xSemaphoreCreateMutex();
}

int http_get(const char *url, char *buf, size_t buf_len)
{
    if (s_gate) xSemaphoreTake(s_gate, portMAX_DELAY);

    int result = -1;
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS-Zertifikatspruefung
        .user_agent = "esp-infoscreen",
        // Groessere Puffer: lange URLs (z.B. Telegram sendMessage mit langem
        // Text) sprengen sonst den TX-Default (512 B) -> Request scheitert; und
        // manche Antwort-Header (Telegram) passen nicht in den RX-Default
        // ("Buffer length is small to fit all the headers").
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c) {
        esp_err_t err = esp_http_client_open(c, 0);
        if (err == ESP_OK) {
            esp_http_client_fetch_headers(c);
            int status = esp_http_client_get_status_code(c);
            int total = 0, r;
            while ((r = esp_http_client_read(c, buf + total, buf_len - 1 - total)) > 0) {
                total += r;
                if (total >= (int)buf_len - 1) break;
            }
            buf[total > 0 ? total : 0] = '\0';
            esp_http_client_close(c);
            if (status == 200) result = total;
            else ESP_LOGW(TAG, "GET %s -> HTTP %d", url, status);
        } else {
            ESP_LOGW(TAG, "GET %s -> open fehlgeschlagen: %s", url, esp_err_to_name(err));
        }
        esp_http_client_cleanup(c);
    }

    if (s_gate) xSemaphoreGive(s_gate);
    return result;
}
