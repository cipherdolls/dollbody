# CipherDolls Dollbody Firmware

ESP-IDF v5.2.1 firmware for the **SenseCAP Watcher** — the physical companion device for the CipherDolls platform. Connects to the backend over WiFi/MQTT and plays AI-generated audio responses through the onboard speaker.

**Hardware:** Seeed SenseCAP Watcher · ESP32-S3 @ 240 MHz · 32 MB Flash · 8 MB PSRAM · 412×412 display · ES8311 audio codec · WS2812 LED

---

## Features

- **WiFi provisioning** — BLE-assisted setup on first boot; credentials saved to NVS
- **Device registration** — HTTPS handshake with backend, retrieves doll ID and linked chat ID
- **MQTT** — subscribes to `chats/{chatId}/actionEvents` and `dolls/{dollId}/actionEvents`
- **Audio playback** — downloads MP3 from backend, decodes with minimp3, outputs via ES8311 codec over I2S
- **LVGL display** — 412×412 UI with boot, provisioning, connecting, idle, and playing states; live MQTT TX/RX indicator dots
- **WS2812 RGB LED** — solid white on idle, off in deep sleep
- **Deep sleep** — automatic after 300 s of inactivity, remotely triggerable via MQTT

---

## Prerequisites

- [ESP-IDF v5.2.1](https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32s3/get-started/index.html) installed and sourced
- USB-C cable connected to the SenseCAP Watcher (appears as `/dev/ttyACM*`)
- A running CipherDolls backend (see `devops/local/`)

---

## Getting Started

### 1. Configure secrets

```bash
cp .env.example .env
```

Edit `.env`:

```env
SECRET_SSID=your_wifi_ssid
SECRET_PASSWORD=your_wifi_password
SECRET_APIKEY=your_api_key            # Bearer token for the backend API
SECRET_DOLL_BODY_ID=doll_body_id      # DollBody ID from the backend
SECRET_SERVER_URL=https://api.cipherdolls.com
SECRET_MQTT_URL=mqtt://api.cipherdolls.com:1883
```

Secrets are injected as compile-time `#define` constants via CMake — they are not stored in any tracked file.

### 2. Build

```bash
source .env
idf.py build
```

### 3. Flash and monitor

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

To erase NVS (wipe saved WiFi credentials and doll ID):

```bash
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash
```

---

## Boot Flow

```
Power on
  │
  ├─ NVS load (saved config)
  ├─ Display init (IO expander → backlight → LVGL)
  ├─ LED init
  ├─ WiFi init
  │
  ├─ Not provisioned?
  │     └─ BLE provisioning UI  →  save SSID/password  →  reboot
  │
  └─ Provisioned
        ├─ Connect WiFi (20 s timeout)
        ├─ audio_init()          — allocate PSRAM decode buffers + task
        ├─ http_sync_doll()      — HTTPS register/verify, fetch chatId, sync SNTP
        ├─ mqtt_start()          — connect to broker, subscribe to topics
        └─ power_task()          — deep sleep watchdog (300 s idle)
```

---

## MQTT Integration

After connecting, the device subscribes to two topics:

| Topic | Direction | Purpose |
|---|---|---|
| `chats/{chatId}/actionEvents` | Receive | Audio play / stop commands |
| `dolls/{dollId}/actionEvents` | Receive | System commands (deep sleep, restart) |
| `dolls/{dollId}/metrics` | Publish | WiFi RSSI, free heap, status (every 5 s) |
| `connections` | Publish | Online / offline presence |

### Handled action events

```json
{ "type": "audio", "action": "play",   "messageId": "..." }
{ "type": "audio", "action": "replay", "messageId": "..." }
{ "type": "audio", "action": "stop" }

{ "type": "system", "action": "deepsleep" }
{ "type": "system", "action": "restart" }
```

---

## Audio Pipeline

```
MQTT play event
  → display "New message! Downloading..."
  → HTTPS GET /messages/{id}/audio        (512 KB PSRAM buffer)
  → minimp3 frame decode                  (32 KB PSRAM task stack)
      mono → interleaved stereo expansion
  → I2S Philips format (I2S_NUM_0)
  → ES8311 codec (I2C 0x18, volume 70)
  → speaker
  → display restored to idle state
```

The audio task stack and all decode buffers (`mp3dec_t`, PCM, stereo expansion) live in **PSRAM** to keep internal SRAM free for TLS, WiFi, and LVGL.

---

## Display States

| State | Shown when |
|---|---|
| `BOOT` | Startup initialisation |
| `WIFI_PROV` | First-boot provisioning |
| `WIFI_CONNECTING` | Connecting to saved SSID |
| `WIFI_OK` | Connected — shows Doll ID and Chat ID |
| `PROCESSING` | Message downloading |
| `PLAYING` | MP3 decoding and playback |
| `ERROR` | WiFi failed, hardware error |

Two small indicator dots in the corner blink on every MQTT TX (outbound) and RX (inbound) event.

---

## Configuration (NVS)

Credentials saved to NVS flash after provisioning:

| Key | Description |
|---|---|
| `ssid` | WiFi network name |
| `password` | WiFi password |
| `doll_id` | Assigned doll UUID (from backend) |
| `apikey` | Bearer token |
| `server_url` | Backend HTTPS base URL |
| `mqtt_url` | MQTT broker URL |

`chat_id` is fetched at runtime on each boot (not persisted).

---

## Flash Partitions

```
nvs       0x9000    24 KB   NVS storage (config)
phy_init  0xf000     4 KB   RF calibration
factory   0x10000    8 MB   Application binary
storage   0x810000   ~8 MB  SPIFFS (reserved)
```

---

## PSRAM Memory Strategy

Internal SRAM is scarce (~200 KB free after WiFi + TLS stack). All large allocations go to PSRAM:

| Allocation | Size | Location |
|---|---|---|
| MP3 download buffer | 512 KB | PSRAM |
| `mp3dec_t` decoder state | ~7 KB | PSRAM |
| PCM decode buffer | ~9 KB | PSRAM |
| Stereo expand buffer | ~9 KB | PSRAM |
| Audio task stack | 32 KB | PSRAM (`CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`) |
| LVGL draw buffers | ~400 KB | PSRAM |

---

## Project Structure

```
dollbody/
├── main/
│   ├── app_main.c        # Entry point, boot sequence
│   ├── board.h           # All GPIO and peripheral constants
│   ├── audio.c/h         # MP3 download + decode + I2S playback
│   ├── display.c/h       # LVGL UI driver
│   ├── mqtt.c/h          # MQTT client, action event handler
│   ├── http.c/h          # HTTPS sync (register, chatId, SNTP)
│   ├── wifi_mgr.c/h      # WiFi station management
│   ├── wifi_prov.c/h     # BLE provisioning
│   ├── config.c/h        # Runtime config struct
│   ├── config_store.c/h  # NVS persistence
│   ├── led.c/h           # WS2812 LED
│   ├── touch.c/h         # Touch input (provisioning)
│   ├── power.c/h         # Deep sleep management
│   ├── events.h          # FreeRTOS event group bit definitions
│   └── minimp3.h         # Single-header MP3 decoder
├── components/
│   ├── pca9535_ioexp/    # I/O expander driver (power, touch INT)
│   └── sscma_client/     # SSCMA AI camera client
├── partitions.csv        # Custom flash partition table
├── sdkconfig.defaults    # Chip and peripheral Kconfig defaults
├── idf_component.yml     # Managed component dependencies
├── .env.example          # Secret configuration template
└── HARDWARE.md           # Full GPIO pin map and hardware reference
```

---

## Hardware Reference

See [HARDWARE.md](HARDWARE.md) for the full GPIO pin map, I2C bus layout, audio codec addresses, and CPU core assignment.

---

## Troubleshooting

**`invalid segment length 0xffffffff` on boot** — Flash corruption, usually from an interrupted flash operation.
```bash
idf.py -p /dev/ttyACM0 erase-flash && idf.py -p /dev/ttyACM0 flash
```

**MQTT dots stop blinking** — Audio task stack too large in DRAM, starving LVGL semaphore. Ensure `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` is in `sdkconfig.defaults` and the audio task stack is allocated from PSRAM.

**No sound / distorted audio** — ES8311 codec not initialised or wrong I2S slot format. The firmware uses Philips (standard I2S) format — do not change to MSB-justified.

**TLS handshake fails** — Device clock at epoch 0. SNTP sync runs during `http_sync_doll()`; check WiFi is connected and `CONFIG_MBEDTLS_HARDWARE_MPI=n` is set (all hardware interrupt slots are occupied by peripherals).

**Guru Meditation / DoubleException during audio** — Stack overflow in minimp3 decoder (`float grbuf[2][576]` = 4 KB on stack). Audio task needs ≥ 32 KB stack.
