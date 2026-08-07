#include "http_util.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "http";

int http_get(const char *url, char *buf, size_t buf_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS-Zertifikatspruefung
        .user_agent = "esp-infoscreen",
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;

    int result = -1;
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
    return result;
}
