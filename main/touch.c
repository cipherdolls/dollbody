#include "touch.h"
#include "board.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_lcd_touch_spd2010.h"
#include "lvgl.h"

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp = NULL;

// I2C bus recovery: 9 SCL pulses + STOP + reinstall
static void touch_i2c_bus_recover(void)
{
    i2c_driver_delete(TOUCH_I2C_PORT);
    gpio_set_direction(TOUCH_I2C_SCL, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(TOUCH_I2C_SDA, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(TOUCH_I2C_SDA, 1);
    for (int i = 0; i < 9; i++) {
        gpio_set_level(TOUCH_I2C_SCL, 0); vTaskDelay(1);
        gpio_set_level(TOUCH_I2C_SCL, 1); vTaskDelay(1);
    }
    gpio_set_level(TOUCH_I2C_SDA, 0); vTaskDelay(1);
    gpio_set_level(TOUCH_I2C_SCL, 0); vTaskDelay(1);
    gpio_set_level(TOUCH_I2C_SCL, 1); vTaskDelay(1);
    gpio_set_level(TOUCH_I2C_SDA, 1); vTaskDelay(1);

    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_I2C_SDA,
        .scl_io_num       = TOUCH_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ,
    };
    i2c_param_config(TOUCH_I2C_PORT, &cfg);
    i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(s_tp);
    bool pressed = esp_lcd_touch_get_coordinates(s_tp, &x, &y, NULL, &cnt, 1);
    if (pressed && cnt > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t touch_init(void)
{
    // SPD2010 touch is on I2C_NUM_1 (separate from audio bus)
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_I2C_SDA,
        .scl_io_num       = TOUCH_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ,
    };
    i2c_param_config(TOUCH_I2C_PORT, &cfg);
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_SPD2010_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)TOUCH_I2C_PORT, &tp_io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max       = LCD_H_RES,
        .y_max       = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_spd2010(tp_io, &tp_cfg, &s_tp));

    // SPD2010 boots in BIOS mode. The driver sends clear_int + cpu_start once
    // to trigger the BIOS→CPU firmware load. The load takes ~3 seconds.
    // We trigger it here, then wait, so LVGL reads will see a live controller.
    ESP_LOGI(TAG, "Touch: triggering BIOS->CPU transition...");
    vTaskDelay(pdMS_TO_TICKS(100));      // brief settle after I2C init
    esp_lcd_touch_read_data(s_tp);       // sends clear_int + (200ms) + cpu_start
    ESP_LOGI(TAG, "Touch: waiting for CPU firmware load (3s)...");
    vTaskDelay(pdMS_TO_TICKS(3000));     // wait for CPU firmware to fully load
    ESP_LOGI(TAG, "Touch: init complete");

    // Register with LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Touch init OK");
    return ESP_OK;
}

bool touch_get_point(uint16_t *x, uint16_t *y)
{
    if (!s_tp) return false;
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(s_tp);
    return esp_lcd_touch_get_coordinates(s_tp, x, y, NULL, &cnt, 1) && cnt > 0;
}
