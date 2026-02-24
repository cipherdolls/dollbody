#pragma once
#include "driver/gpio.h"

// --- LCD (SPD2010 QSPI) ---
#define LCD_SPI_HOST        SPI3_HOST
#define LCD_PCLK            GPIO_NUM_7
#define LCD_DATA0           GPIO_NUM_9
#define LCD_DATA1           GPIO_NUM_1
#define LCD_DATA2           GPIO_NUM_14
#define LCD_DATA3           GPIO_NUM_13
#define LCD_CS              GPIO_NUM_45
#define LCD_BL              GPIO_NUM_8
#define LCD_H_RES           412
#define LCD_V_RES           412

// --- Touch (SPD2010 integrated, I2C_NUM_1) ---
#define TOUCH_I2C_PORT      I2C_NUM_1
#define TOUCH_I2C_SDA       GPIO_NUM_39
#define TOUCH_I2C_SCL       GPIO_NUM_38
#define TOUCH_I2C_FREQ      400000
#define TOUCH_ADDR          0x53

// --- Audio I2C (I2C_NUM_0, also used for IO expanders) ---
#define AUDIO_I2C_PORT      I2C_NUM_0
#define AUDIO_I2C_SDA       GPIO_NUM_47
#define AUDIO_I2C_SCL       GPIO_NUM_48
#define AUDIO_I2C_FREQ      100000

// --- IO Expander (PCA9535) on AUDIO_I2C bus ---
// 0x21 = system power + camera; 0x77 = knob
#define IO_EXP_PWR_ADDR     0x21
#define IO_EXP_KNOB_ADDR    0x77

// PCA9535 registers
#define PCA9535_OUTPUT0     0x02
#define PCA9535_OUTPUT1     0x03
#define PCA9535_CONFIG0     0x06
#define PCA9535_CONFIG1     0x07

// --- RGB LED (WS2812) ---
#define LED_GPIO            GPIO_NUM_40
#define LED_COUNT           1

// --- Audio I2S ---
#define I2S_PORT            I2S_NUM_0
#define I2S_MCLK            GPIO_NUM_10
#define I2S_BCLK            GPIO_NUM_11
#define I2S_WS              GPIO_NUM_12
#define I2S_DIN             GPIO_NUM_15
#define I2S_DOUT            GPIO_NUM_16

// --- AI Camera SPI (Himax WE2 via SSCMA) ---
#define CAM_SPI_HOST        SPI2_HOST
#define CAM_SPI_SCLK        GPIO_NUM_4
#define CAM_SPI_MOSI        GPIO_NUM_5
#define CAM_SPI_MISO        GPIO_NUM_6
#define CAM_SPI_CS          GPIO_NUM_21
#define CAM_SPI_CLK_HZ      (12 * 1000 * 1000)
#define CAM_SYNC_PIN        6   // IO expander pin on IO_EXP_PWR_ADDR
#define CAM_RESET_PIN       7
#define CAM_POWER_PIN       11

// --- SD Card ---
#define SD_SPI_CS           GPIO_NUM_46

// --- Rotary Knob ---
#define KNOB_A              GPIO_NUM_41
#define KNOB_B              GPIO_NUM_42
// Knob button is on IO_EXP_KNOB_ADDR pin 3
