#include "wifi_mgr.h"
#include "events.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_mgr";
static bool s_initialized = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected");
        xEventGroupClearBits(g_events, EVT_WIFI_CONNECTED | EVT_WIFI_GOT_IP);
        xEventGroupSetBits(g_events, EVT_WIFI_DISCONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupClearBits(g_events, EVT_WIFI_DISCONNECTED);
        xEventGroupSetBits(g_events, EVT_WIFI_CONNECTED | EVT_WIFI_GOT_IP);
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
    s_initialized = true;
    return ESP_OK;
}

int wifi_mgr_scan(wifi_ap_info_t **out_aps)
{
    *out_aps = NULL;

    wifi_scan_config_t scan_cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed to start");
        return 0;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return 0;
    if (ap_count > 30) ap_count = 30;

    wifi_ap_record_t *records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!records) return 0;

    esp_wifi_scan_get_ap_records(&ap_count, records);

    // Allocate output (max same size, dedup shrinks it)
    wifi_ap_info_t *aps = malloc(ap_count * sizeof(wifi_ap_info_t));
    if (!aps) { free(records); return 0; }

    int unique = 0;
    for (int i = 0; i < ap_count; i++) {
        if (strlen((char *)records[i].ssid) == 0) continue;  // skip hidden

        // Deduplicate — keep entry with strongest signal
        bool found = false;
        for (int j = 0; j < unique; j++) {
            if (strcmp(aps[j].ssid, (char *)records[i].ssid) == 0) {
                if (records[i].rssi > aps[j].rssi) {
                    aps[j].rssi     = records[i].rssi;
                    aps[j].authmode = records[i].authmode;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            strlcpy(aps[unique].ssid, (char *)records[i].ssid, sizeof(aps[unique].ssid));
            aps[unique].rssi     = records[i].rssi;
            aps[unique].authmode = records[i].authmode;
            unique++;
        }
    }

    free(records);
    ESP_LOGI(TAG, "Scan found %d unique networks", unique);
    *out_aps = aps;
    return unique;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    wifi_config_t wcfg = {};
    strlcpy((char *)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    // Accept any security level (open or WPA/WPA2/WPA3)
    wcfg.sta.threshold.authmode = (strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);
    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    return (xEventGroupGetBits(g_events) & EVT_WIFI_GOT_IP) != 0;
}

void wifi_mgr_disconnect(void)
{
    esp_wifi_disconnect();
}
