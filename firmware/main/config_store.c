#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
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

void config_get_str_def(const char *key, char *out, size_t out_len, const char *def)
{
    if (!config_get_str(key, out, out_len) || out[0] == '\0') {
        strncpy(out, def, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

int config_get_int(const char *key, int def)
{
    char buf[16];
    if (config_get_str(key, buf, sizeof(buf)) && buf[0]) return atoi(buf);
    return def;
}

void config_set_int(const char *key, int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    config_set_str(key, buf);
}

void config_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "Konfiguration geloescht (Werksreset)");
    }
}

int config_export_json(char *buf, size_t buf_len)
{
    cJSON *root = cJSON_CreateObject();
    nvs_handle_t h;
    nvs_open(NS, NVS_READONLY, &h);

    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find("nvs", NS, NVS_TYPE_STR, &it);
    char *val = malloc(4096);
    while (err == ESP_OK && it && val) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        size_t len = 4096;
        if (nvs_get_str(h, info.key, val, &len) == ESP_OK) {
            cJSON_AddStringToObject(root, info.key, val);
        }
        err = nvs_entry_next(&it);
    }
    if (val) free(val);
    nvs_close(h);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    int n = -1;
    if (json) {
        if (strlen(json) < buf_len) { strcpy(buf, json); n = (int)strlen(json); }
        cJSON_free(json);
    }
    return n;
}

bool config_import_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    cJSON *e;
    cJSON_ArrayForEach(e, root) {
        if (cJSON_IsString(e) && e->string) config_set_str(e->string, e->valuestring);
    }
    cJSON_Delete(root);
    return true;
}
