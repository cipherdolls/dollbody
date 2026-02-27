#include "config.h"
#include <string.h>

doll_config_t g_config = {
    .ssid        = "",
    .password    = "",
    .apikey      = "",
    .doll_body_id = "",
    .doll_id      = "",
    .server_url   = "https://api.cipherdolls.com",
    .stream_recorder_url = "http://stream-recorder.cipherdolls.com",
    .provisioned = false,
};
