#pragma once

// Starts a FreeRTOS task that registers this device with the backend.
// If doll_id is already stored it fetches the doll instead.
// On success, saves doll_id to NVS and shows it on the display.
void http_sync_doll(void);
