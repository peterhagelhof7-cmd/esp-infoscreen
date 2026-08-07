#include "config_store.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "config_store";
#define NS "cfg"

void config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS neu initialisieren (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool config_get_str(const char *key, char *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK;
}

void config_set_str(const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (write) fehlgeschlagen");
        return;
    }
    esp_err_t err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "Schreiben von '%s' fehlgeschlagen: %s", key, esp_err_to_name(err));
    nvs_close(h);
}

bool config_has_wifi(void)
{
    char ssid[33];
    return config_get_str("wifi_ssid", ssid, sizeof(ssid)) && ssid[0] != '\0';
}

bool config_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (!config_get_str("wifi_ssid", ssid, ssid_len)) return false;
    if (!config_get_str("wifi_pass", pass, pass_len)) pass[0] = '\0';  // offenes Netz erlaubt
    return ssid[0] != '\0';
}

void config_set_wifi(const char *ssid, const char *pass)
{
    config_set_str("wifi_ssid", ssid);
    config_set_str("wifi_pass", pass ? pass : "");
}
