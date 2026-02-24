#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t touch_init(void);
bool touch_get_point(uint16_t *x, uint16_t *y);
