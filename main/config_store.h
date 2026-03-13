#pragma once
#include "esp_err.h"

esp_err_t config_store_load(void);
esp_err_t config_store_save(void);
esp_err_t config_store_save_from_psram(void);  // safe to call from PSRAM-stacked tasks
esp_err_t config_store_clear(void);
