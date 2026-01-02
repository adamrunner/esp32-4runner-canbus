/*
 * Settings Store Implementation
 */

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "settings_store.h"

static const char *TAG = "SETTINGS_STORE";

static const char *k_settings_namespace = "settings";
static const char *k_can_autostart_key = "can_autostart";

static bool s_nvs_ready = false;

static bool ensure_nvs_ready(void)
{
    if (s_nvs_ready) {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(erase_err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS: %s", esp_err_to_name(err));
        return false;
    }

    s_nvs_ready = true;
    return true;
}

bool settings_get_can_autostart(bool *auto_start_out)
{
    if (!auto_start_out) {
        return false;
    }

    *auto_start_out = false;

    if (!ensure_nvs_ready()) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_settings_namespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t value = 0;
    err = nvs_get_u8(handle, k_can_autostart_key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read CAN auto-start flag: %s", esp_err_to_name(err));
    } else {
        *auto_start_out = (value != 0);
    }

    nvs_close(handle);
    return (err == ESP_OK);
}

bool settings_set_can_autostart(bool enable)
{
    if (!ensure_nvs_ready()) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_settings_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(handle, k_can_autostart_key, enable ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write CAN auto-start flag: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit CAN auto-start flag: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    return true;
}
