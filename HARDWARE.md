# SenseCAP Watcher - Hardware Reference

## Overview

- **MCU**: ESP32-S3 (QFN56), dual-core Xtensa LX7 @ 240MHz
- **Flash**: 32MB QIO 80MHz
- **PSRAM**: 8MB Octal SPI 80MHz
- **Framework**: ESP-IDF v5.2.1

## GPIO Pin Map

### LCD (SPD2010 QSPI)

| Signal | GPIO | Notes |
|--------|------|-------|
| PCLK (SPI CLK) | 7 | SPI3_HOST |
| DATA0 | 9 | Quad SPI data |
| DATA1 | 1 | Quad SPI data |
| DATA2 | 14 | Quad SPI data |
| DATA3 | 13 | Quad SPI data |
| CS | 45 | Chip select |
| BL | 8 | Backlight (LEDC PWM) |

- **Resolution**: 412 x 412, RGB565
- **Pixel clock**: 40 MHz
- **SPI host**: SPI3_HOST
- **SPI mode**: 3 (CPOL=1, CPHA=1)
- **Panel driver**: `espressif/esp_lcd_spd2010`

### Touch (SPD2010 integrated)

| Signal | GPIO | Notes |
|--------|------|-------|
| I2C SDA | 39 | I2C_NUM_1 |
| I2C SCL | 38 | I2C_NUM_1 |
| INT | IO Expander pin 5 | Via PCA9535 (not direct GPIO) |
| RST | NC | Not connected |

- **I2C address**: 0x53
- **I2C speed**: 400 kHz
- **Driver**: `espressif/esp_lcd_touch_spd2010`
- **Note**: Touch controller is integrated into the SPD2010 LCD IC, not a separate CHSC6x

### Audio I2S

| Signal | GPIO | Notes |
|--------|------|-------|
| MCLK | 10 | Master clock |
| BCLK | 11 | Bit clock |
| WS | 12 | Word select (LRCLK) |
| DIN | 15 | Mic -> ESP (ADC input) |
| DOUT | 16 | ESP -> Speaker (DAC output) |

- **I2S port**: I2S_NUM_0
- **Sample rate**: 16 kHz
- **Bit depth**: 16-bit
- **Channels**: Mono

### Audio I2C (Codec Bus)

| Signal | GPIO | Notes |
|--------|------|-------|
| I2C SDA | 47 | I2C_NUM_0 |
| I2C SCL | 48 | I2C_NUM_0 |

- **I2C speed**: 100 kHz

Devices on this bus:

| Device | Address | Function |
|--------|---------|----------|
| ES8311 | 0x30 | DAC (speaker output) |
| ES7243 | 0x13 | ADC (microphone input) |
| PCA9535 | 0x77 | 16-bit I/O expander |

### RGB LED (WS2812)

| Signal | GPIO | Notes |
|--------|------|-------|
| Data | 40 | NeoPixel single LED |

- **LED count**: 1

### Rotary Knob

| Signal | GPIO | Notes |
|--------|------|-------|
| Encoder A | 41 | Quadrature input |
| Encoder B | 42 | Quadrature input |
| Button | PCA9535 pin 3 | Via I/O expander |

## I2C Bus Summary

| Bus | Port | SDA | SCL | Speed | Devices |
|-----|------|-----|-----|-------|---------|
| Audio/General | I2C_NUM_0 | GPIO 47 | GPIO 48 | 100 kHz | ES8311 (0x30), ES7243 (0x13), PCA9535 (0x77) |
| Touch | I2C_NUM_1 | GPIO 39 | GPIO 38 | 400 kHz | SPD2010 touch (0x53) |

## CPU Core Assignment

| Core | Tasks |
|------|-------|
| Core 0 (SYS_CPU) | WiFi, HTTP, MQTT, Display, WiFi Prov, Power |
| Core 1 (APP_CPU) | Audio Record, Audio Play, Input, LED |

## Power

- USB-C powered via CH344 USB-UART bridge
- Serial port: /dev/ttyACM2 (115200 baud)
- Deep sleep supported with configurable timeout (default 300s)
