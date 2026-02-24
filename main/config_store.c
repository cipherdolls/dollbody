#include "config_store.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_store";
#define NVS_NAMESPACE "doll_cfg"

esp_err_t config_store_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        return ESP_OK;
    }
    ESP_ERROR_CHECK(err);

    size_t len;

    len = sizeof(g_config.ssid);
    nvs_get_str(h, "ssid", g_config.ssid, &len);

    len = sizeof(g_config.password);
    nvs_get_str(h, "password", g_config.password, &len);

    len = sizeof(g_config.apikey);
    nvs_get_str(h, "apikey", g_config.apikey, &len);

    len = sizeof(g_config.doll_id);
    nvs_get_str(h, "doll_id", g_config.doll_id, &len);

    len = sizeof(g_config.server_url);
    nvs_get_str(h, "server_url", g_config.server_url, &len);

    uint8_t prov = 0;
    nvs_get_u8(h, "provisioned", &prov);
    g_config.provisioned = (prov != 0);

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded: ssid='%s' provisioned=%d", g_config.ssid, g_config.provisioned);
    return ESP_OK;
}

esp_err_t config_store_save(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));

    nvs_set_str(h, "ssid",       g_config.ssid);
    nvs_set_str(h, "password",   g_config.password);
    nvs_set_str(h, "apikey",     g_config.apikey);
    nvs_set_str(h, "doll_id",    g_config.doll_id);
    nvs_set_str(h, "server_url", g_config.server_url);
    nvs_set_u8 (h, "provisioned", g_config.provisioned ? 1 : 0);

    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Config saved");
    return err;
}

esp_err_t config_store_clear(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    memset(&g_config, 0, sizeof(g_config));
    strcpy(g_config.server_url, "https://api.cipherdolls.com");
    ESP_LOGI(TAG, "Config cleared");
    return ESP_OK;
}
