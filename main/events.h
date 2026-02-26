#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Global event group bits
#define EVT_WIFI_CONNECTED      (1 << 0)
#define EVT_WIFI_DISCONNECTED   (1 << 1)
#define EVT_WIFI_GOT_IP         (1 << 2)
#define EVT_MQTT_CONNECTED      (1 << 3)
#define EVT_MQTT_DISCONNECTED   (1 << 4)
#define EVT_PROV_DONE           (1 << 5)
#define EVT_AUDIO_PLAYING       (1 << 6)
#define EVT_AUDIO_RECORDING     (1 << 7)
#define EVT_DEEP_SLEEP          (1 << 8)
#define EVT_DOLL_READY          (1 << 9)   // doll_id confirmed with backend

extern EventGroupHandle_t g_events;
