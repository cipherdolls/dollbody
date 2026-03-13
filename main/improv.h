#pragma once

// Start the Improv Wi-Fi Serial listener task.
// Runs continuously on UART0, parsing Improv packets alongside ESP_LOG output.
// On receiving Wi-Fi credentials, connects and saves to NVS.
void improv_task_fn(void *pvParameter);
