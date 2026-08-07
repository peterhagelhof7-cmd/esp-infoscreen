#include "ota_manager.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "ota";

static esp_ota_handle_t s_handle;
static const esp_partition_t *s_target;
static bool s_in_progress;

void ota_manager_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        ESP_LOGI(TAG, "Laufende Partition '%s', Status %d", running->label, (int)state);
    } else {
        ESP_LOGI(TAG, "Laufende Partition '%s'", running->label);
    }
}

bool ota_manager_begin(void)
{
    if (s_in_progress) { ESP_LOGW(TAG, "Update laeuft bereits"); return false; }
    s_target = esp_ota_get_next_update_partition(NULL);
    if (!s_target) { ESP_LOGE(TAG, "Kein Ziel-Slot gefunden"); return false; }
    esp_err_t err = esp_ota_begin(s_target, OTA_SIZE_UNKNOWN, &s_handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err)); return false; }
    s_in_progress = true;
    ESP_LOGI(TAG, "OTA gestartet -> Slot '%s'", s_target->label);
    return true;
}

bool ota_manager_write(const uint8_t *data, size_t len)
{
    if (!s_in_progress) return false;
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
        ota_manager_abort();
        return false;
    }
    return true;
}

bool ota_manager_finish(void)
{
    if (!s_in_progress) return false;
    s_in_progress = false;
    esp_err_t err = esp_ota_end(s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s (Image ungueltig?)", esp_err_to_name(err));
        return false;
    }
    err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "OTA abgeschlossen, Boot-Partition auf '%s' gesetzt", s_target->label);
    return true;
}

void ota_manager_abort(void)
{
    if (s_in_progress) {
        esp_ota_abort(s_handle);
        s_in_progress = false;
        ESP_LOGW(TAG, "OTA abgebrochen");
    }
}

void ota_manager_mark_valid(void)
{
    // Bootloader-Rollback abbrechen: die laufende (evtl. frisch geflashte) App
    // gilt jetzt als funktionsfaehig. Ohne diesen Aufruf rollt der Bootloader
    // beim naechsten Neustart auf die vorige App zurueck.
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) ESP_LOGI(TAG, "App als gueltig markiert (Rollback abgebrochen)");
    else ESP_LOGW(TAG, "mark_app_valid: %s", esp_err_to_name(err));
}
