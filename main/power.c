#include "power.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "power";
#define SLEEP_TIMEOUT_MS  (5 * 60 * 1000)  // 5 minutes

static volatile uint32_t s_last_activity_ms = 0;

void power_reset_sleep_timer(void)
{
    s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void power_task_fn(void *pvParameter)
{
    power_reset_sleep_timer();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if ((now - s_last_activity_ms) >= SLEEP_TIMEOUT_MS) {
            ESP_LOGI(TAG, "Inactivity timeout, entering deep sleep");
            xEventGroupSetBits(g_events, EVT_DEEP_SLEEP);
            esp_deep_sleep_start();
        }
    }
}
