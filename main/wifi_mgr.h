#pragma once
#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>

typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_info_t;

esp_err_t wifi_mgr_init(void);
// Scan for access points. Returns count (0 on failure). Caller must free() result.
int wifi_mgr_scan(wifi_ap_info_t **out_aps);
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);
bool wifi_mgr_is_connected(void);
void wifi_mgr_disconnect(void);
