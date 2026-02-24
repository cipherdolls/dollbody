idf_component_register(
    SRCS "app_main.c"
         "config.c"
         "config_store.c"
         "display.c"
         "touch.c"
         "led.c"
         "wifi_mgr.c"
         "wifi_prov.c"
         "power.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_psram esp_wifi nvs_flash esp_event esp_lcd
             esp_netif esp_timer freertos
             espressif__led_strip
)
