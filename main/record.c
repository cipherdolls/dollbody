#include "record.h"
#include "board.h"
#include "config.h"
#include "events.h"
#include "display.h"
#include "touch.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "record";

#define SAMPLE_RATE      16000
#define RECORD_MAX_S     20
#define I2S_READ_BYTES   2048   // stereo read buffer per iteration
#define PREBUF_BYTES     (SAMPLE_RATE * 2 * 3)  // 3 seconds mono @ 16kHz 16-bit = 96 KB

#define ES7243_ADDR  0x14   // Confirmed by I2C scan on SenseCAP Watcher

static i2s_chan_handle_t s_rx_chan  = NULL;
static bool             s_mic_init = false;

// ── WAV header ────────────────────────────────────────────────────────────────

typedef struct __attribute__((packed)) {
    char     riff[4];           // "RIFF"
    uint32_t file_size;         // total file size - 8
    char     wave[4];           // "WAVE"
    char     fmt_id[4];         // "fmt "
    uint32_t fmt_size;          // 16
    uint16_t audio_format;      // 1 = PCM
    uint16_t channels;          // 1
    uint32_t sample_rate;       // 16000
    uint32_t byte_rate;         // sample_rate * channels * bits/8
    uint16_t block_align;       // channels * bits/8
    uint16_t bits_per_sample;   // 16
    char     data_id[4];        // "data"
    uint32_t data_size;         // PCM byte count
} wav_hdr_t;

static void build_wav_header(wav_hdr_t *h, uint32_t pcm_bytes)
{
    memcpy(h->riff,    "RIFF", 4);
    h->file_size       = pcm_bytes + sizeof(wav_hdr_t) - 8;
    memcpy(h->wave,    "WAVE", 4);
    memcpy(h->fmt_id,  "fmt ", 4);
    h->fmt_size        = 16;
    h->audio_format    = 1;
    h->channels        = 1;
    h->sample_rate     = SAMPLE_RATE;
    h->byte_rate       = SAMPLE_RATE * 2;   // 1 ch × 2 bytes
    h->block_align     = 2;
    h->bits_per_sample = 16;
    memcpy(h->data_id, "data", 4);
    h->data_size       = pcm_bytes;
}

// ── ES7243E ADC init ──────────────────────────────────────────────────────────

static void es7243_write(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES7243_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(AUDIO_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES7243E write reg 0x%02X failed: %s", reg, esp_err_to_name(err));
    }
}

static void es7243e_init(void)
{
    // Chip at 0x14 is ES7243E (ID 0x7A43), NOT plain ES7243.
    // Sequence from ESP-ADF es7243e driver — paged register map.
    es7243_write(0x01, 0x3A);
    es7243_write(0x00, 0x80);   // Reset all registers
    vTaskDelay(pdMS_TO_TICKS(10));
    es7243_write(0xF9, 0x00);   // Select page 0
    es7243_write(0x04, 0x02);
    es7243_write(0x04, 0x01);
    es7243_write(0xF9, 0x01);   // Select page 1
    es7243_write(0x00, 0x1E);
    es7243_write(0x01, 0x00);
    es7243_write(0x02, 0x00);
    es7243_write(0x03, 0x20);
    es7243_write(0x04, 0x01);
    es7243_write(0x0D, 0x00);
    es7243_write(0x05, 0x00);
    es7243_write(0x06, 0x03);   // SCLK = MCLK / 4
    es7243_write(0x07, 0x00);   // LRCK = MCLK / 256 (high byte)
    es7243_write(0x08, 0xFF);   // LRCK = MCLK / 256 (low byte)
    es7243_write(0x09, 0xCA);
    es7243_write(0x0A, 0x85);
    es7243_write(0x0B, 0x00);
    es7243_write(0x0E, 0xBF);
    es7243_write(0x0F, 0x80);
    es7243_write(0x14, 0x0C);
    es7243_write(0x15, 0x0C);
    es7243_write(0x17, 0x02);
    es7243_write(0x18, 0x26);
    es7243_write(0x19, 0x77);
    es7243_write(0x1A, 0xF4);
    es7243_write(0x1B, 0x66);
    es7243_write(0x1C, 0x44);
    es7243_write(0x1E, 0x00);
    es7243_write(0x1F, 0x0C);
    es7243_write(0x20, 0x1A);  // MIC PGA gain +30 dB
    es7243_write(0x21, 0x1A);  // MIC PGA gain +30 dB
    es7243_write(0x00, 0x80);  // Slave mode, enable
    es7243_write(0x01, 0x3A);
    es7243_write(0x16, 0x3F);
    es7243_write(0x16, 0x00);
    ESP_LOGI(TAG, "ES7243E init done (addr=0x%02X, chip ID 0x7A43)", ES7243_ADDR);
}

// ── Knob button — press to record, release to send ──────────────────────────
// Button is on PCA9535 IO expander at 0x21 (IO_EXP_ADDR), port 0, pin 3.
// Active low — pressed = bit clear.

static bool s_knob_btn_ok = false;   // set true once first read succeeds

static bool knob_btn_pressed(void)
{
    uint8_t reg = PCA9535_INPUT0;
    uint8_t val = 0xFF;
    esp_err_t err = i2c_master_write_read_device(
        AUDIO_I2C_PORT, IO_EXP_ADDR,
        &reg, 1, &val, 1, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        if (s_knob_btn_ok) {
            ESP_LOGW(TAG, "Knob I2C read failed: %s", esp_err_to_name(err));
        }
        return false;
    }
    if (!s_knob_btn_ok) {
        s_knob_btn_ok = true;
        ESP_LOGI(TAG, "Knob IO expander 0x%02X responding, port0=0x%02X",
                 IO_EXP_ADDR, val);
    }
    return !(val & (1 << KNOB_BTN_BIT));   // active low
}

static void knob_init(void)
{
    // Configure port 0 pin 3 as input on the knob IO expander
    uint8_t cmd[] = { PCA9535_CONFIG0, (1 << KNOB_BTN_BIT) };
    esp_err_t err = i2c_master_write_to_device(
        AUDIO_I2C_PORT, IO_EXP_ADDR,
        cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Knob IO expander 0x%02X not found (%s) — button disabled",
                 IO_EXP_ADDR, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Knob button configured (IO exp 0x%02X, port0 pin %d)",
                 IO_EXP_ADDR, KNOB_BTN_BIT);
    }
}

// ── I2S RX ────────────────────────────────────────────────────────────────────

static void i2s_rx_start(void)
{
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cfg, NULL, &s_rx_chan));

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws   = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));
    ESP_LOGI(TAG, "I2S RX started at %d Hz mono", SAMPLE_RATE);
}

static void i2s_rx_stop(void)
{
    if (s_rx_chan) {
        i2s_channel_disable(s_rx_chan);
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        ESP_LOGI(TAG, "I2S RX stopped");
    }
}

// ── WebSocket event helpers ──────────────────────────────────────────────────

static EventGroupHandle_t s_ws_events;
#define WS_EVT_CONNECTED   (1 << 0)
#define WS_EVT_CLOSED      (1 << 1)
#define WS_EVT_ERROR       (1 << 2)

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        xEventGroupSetBits(s_ws_events, WS_EVT_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WS disconnected");
        xEventGroupSetBits(s_ws_events, WS_EVT_CLOSED);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        xEventGroupSetBits(s_ws_events, WS_EVT_ERROR);
        break;
    default:
        break;
    }
}

// ── Display helper ────────────────────────────────────────────────────────────

static void restore_idle_display(void)
{
    char msg[128];
    if (strlen(g_config.chat_id) > 0) {
        snprintf(msg, sizeof(msg), "Doll ID:\n%.36s\nChat ID:\n%.36s",
                 g_config.doll_id, g_config.chat_id);
    } else {
        snprintf(msg, sizeof(msg), "Doll ID:\n%.36s\nNo chat linked",
                 g_config.doll_id);
    }
    display_set_state(DISPLAY_STATE_WIFI_OK, msg);
}

// ── Record task — true streaming with pre-buffering ──────────────────────────
// On button press: start I2S immediately into a pre-buffer while WSS connects.
// Once connected: flush pre-buffer → continue streaming live.
// On button release: stop I2S → close WS (triggers server-side processing).

static void record_task(void *arg)
{
    // touch_init() blocks ~3 s for the SPD2010 BIOS→CPU firmware transition
    touch_init();

    // Configure knob button on IO expander
    knob_init();

    // Small buffer for I2S reads
    uint8_t *i2s_buf = malloc(I2S_READ_BYTES);
    // Pre-buffer in PSRAM to capture audio during TLS handshake (~1-2 s)
    uint8_t *prebuf = heap_caps_malloc(PREBUF_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!i2s_buf || !prebuf) {
        ESP_LOGE(TAG, "Failed to allocate record buffers");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Ready — %d s max, %d KB pre-buffer, streaming to: %s",
             RECORD_MAX_S, PREBUF_BYTES / 1024, g_config.stream_recorder_url);

    while (1) {
        // ── Wait for knob button press ───────────────────────────────────────
        while (!knob_btn_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(30));
        }

        // Ignore while audio is playing or another record is in progress
        EventBits_t bits = xEventGroupGetBits(g_events);
        if (bits & (EVT_AUDIO_PLAYING | EVT_AUDIO_RECORDING)) {
            while (knob_btn_pressed()) vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        // Need a linked chat to send to
        if (strlen(g_config.chat_id) == 0) {
            ESP_LOGW(TAG, "No chat linked, ignoring knob press");
            while (knob_btn_pressed()) vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        xEventGroupSetBits(g_events, EVT_AUDIO_RECORDING);
        display_set_state(DISPLAY_STATE_RECORDING, "Recording...");

        // ── Start I2S + mic IMMEDIATELY — capture audio while WS connects ───
        i2s_rx_start();
        if (!s_mic_init) {
            vTaskDelay(pdMS_TO_TICKS(50));
            es7243e_init();
            s_mic_init = true;
        }

        // ── Pre-buffer: capture mono audio while WS handshake happens ────────
        size_t prebuf_fill = 0;

        // Build WS URL: convert https:// → wss:// (or http:// → ws://)
        char url[384];
        if (strncmp(g_config.stream_recorder_url, "https://", 8) == 0) {
            snprintf(url, sizeof(url), "wss://%s/ws-stream?chatId=%s&auth=%s",
                     g_config.stream_recorder_url + 8, g_config.chat_id, g_config.apikey);
        } else if (strncmp(g_config.stream_recorder_url, "http://", 7) == 0) {
            snprintf(url, sizeof(url), "ws://%s/ws-stream?chatId=%s&auth=%s",
                     g_config.stream_recorder_url + 7, g_config.chat_id, g_config.apikey);
        } else {
            snprintf(url, sizeof(url), "ws://%s/ws-stream?chatId=%s&auth=%s",
                     g_config.stream_recorder_url, g_config.chat_id, g_config.apikey);
        }

        s_ws_events = xEventGroupCreate();
        esp_websocket_client_config_t ws_cfg = { .uri = url };
        if (strncmp(url, "wss://", 6) == 0) {
            ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
        }

        esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                       ws_event_handler, NULL);
        esp_websocket_client_start(client);

        // Record into pre-buffer while waiting for WS connection
        ESP_LOGI(TAG, "Pre-buffering while WS connects...");
        bool ws_connected = false;
        while (prebuf_fill < PREBUF_BYTES && knob_btn_pressed()) {
            // Check if WS connected yet (non-blocking)
            bits = xEventGroupWaitBits(s_ws_events,
                WS_EVT_CONNECTED | WS_EVT_ERROR,
                pdTRUE, pdFALSE, 0);
            if (bits & (WS_EVT_CONNECTED | WS_EVT_ERROR)) {
                ws_connected = !!(bits & WS_EVT_CONNECTED);
                break;
            }

            size_t got = 0;
            i2s_channel_read(s_rx_chan, i2s_buf, I2S_READ_BYTES,
                             &got, pdMS_TO_TICKS(200));
            if (got == 0) continue;

            // Downsample stereo→mono in-place (keep right channel)
            int16_t *s = (int16_t *)i2s_buf;
            size_t n_stereo = got / 2;
            size_t n_mono   = n_stereo / 2;
            for (size_t i = 0; i < n_mono; i++) {
                s[i] = s[i * 2 + 1];
            }
            size_t mono_bytes = n_mono * 2;
            size_t to_copy = mono_bytes;
            if (prebuf_fill + to_copy > PREBUF_BYTES)
                to_copy = PREBUF_BYTES - prebuf_fill;
            memcpy(prebuf + prebuf_fill, i2s_buf, to_copy);
            prebuf_fill += to_copy;
        }

        // If WS didn't connect during pre-buffer, wait a bit more
        if (!ws_connected) {
            bits = xEventGroupWaitBits(s_ws_events,
                WS_EVT_CONNECTED | WS_EVT_ERROR,
                pdTRUE, pdFALSE, pdMS_TO_TICKS(8000));
            ws_connected = !!(bits & WS_EVT_CONNECTED);
        }

        if (!ws_connected) {
            ESP_LOGE(TAG, "WS connect failed");
            i2s_rx_stop();
            esp_websocket_client_destroy(client);
            vEventGroupDelete(s_ws_events);
            xEventGroupClearBits(g_events, EVT_AUDIO_RECORDING);
            restore_idle_display();
            while (knob_btn_pressed()) vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        ESP_LOGI(TAG, "WS connected, flushing %zu bytes pre-buffer", prebuf_fill);

        // ── Send WAV header + flush pre-buffer ──────────────────────────────
        wav_hdr_t hdr;
        build_wav_header(&hdr, RECORD_MAX_S * SAMPLE_RATE * 2);
        esp_websocket_client_send_bin(client, (const char *)&hdr,
                                      sizeof(wav_hdr_t), pdMS_TO_TICKS(5000));

        bool ws_ok = true;
        size_t total_mono = 0;

        // Flush pre-buffer
        if (prebuf_fill > 0) {
            int sent = esp_websocket_client_send_bin(client,
                (const char *)prebuf, prebuf_fill, pdMS_TO_TICKS(5000));
            if (sent < 0) {
                ESP_LOGE(TAG, "WS send pre-buffer failed");
                ws_ok = false;
            }
            total_mono += prebuf_fill;
        }

        display_set_state(DISPLAY_STATE_RECORDING, "Recording...\nRelease to stop");
        ESP_LOGI(TAG, "Streaming audio...");

        // ── Stream loop: read I2S → downsample → send via WS ────────────────
        size_t max_mono = RECORD_MAX_S * SAMPLE_RATE * 2;

        while (ws_ok && total_mono < max_mono) {
            if (!knob_btn_pressed()) break;

            size_t got = 0;
            i2s_channel_read(s_rx_chan, i2s_buf, I2S_READ_BYTES,
                             &got, pdMS_TO_TICKS(200));
            if (got == 0) continue;

            // Downsample stereo→mono in-place (keep right channel)
            int16_t *s = (int16_t *)i2s_buf;
            size_t n_stereo_samples = got / 2;
            size_t n_mono_samples   = n_stereo_samples / 2;
            for (size_t i = 0; i < n_mono_samples; i++) {
                s[i] = s[i * 2 + 1];
            }
            size_t mono_bytes = n_mono_samples * 2;

            int sent = esp_websocket_client_send_bin(client,
                (const char *)i2s_buf, mono_bytes, pdMS_TO_TICKS(5000));
            if (sent < 0) {
                ESP_LOGE(TAG, "WS send failed at %zu bytes", total_mono);
                ws_ok = false;
                break;
            }
            total_mono += mono_bytes;
        }

        // ── Stop recording ───────────────────────────────────────────────────
        i2s_rx_stop();
        xEventGroupClearBits(g_events, EVT_AUDIO_RECORDING);

        float dur = (float)total_mono / (SAMPLE_RATE * 2);
        ESP_LOGI(TAG, "Streamed %.1f s (%zu B mono, %zu pre-buffered)",
                 dur, total_mono, prebuf_fill);

        // Close WS — triggers server-side WAV→MP3 conversion + forwarding
        esp_websocket_client_close(client, pdMS_TO_TICKS(5000));
        esp_websocket_client_destroy(client);
        vEventGroupDelete(s_ws_events);

        if (!ws_ok || total_mono < SAMPLE_RATE) {
            if (total_mono < SAMPLE_RATE)
                ESP_LOGW(TAG, "Too short (%.1f s), discarded by server", dur);
        }

        restore_idle_display();
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void record_init(void)
{
    xTaskCreatePinnedToCore(record_task, "record", 8192, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "Record task spawned");
}
