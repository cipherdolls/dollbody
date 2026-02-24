#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_mgr_init(void);
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);
bool wifi_mgr_is_connected(void);
void wifi_mgr_disconnect(void);
