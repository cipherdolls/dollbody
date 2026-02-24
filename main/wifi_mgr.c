#include "wifi_mgr.h"
#include "events.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

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

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    wifi_config_t wcfg = {};
    strlcpy((char *)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

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
