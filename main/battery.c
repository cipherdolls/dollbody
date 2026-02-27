#include "battery.h"
#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/i2c.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t         s_cali = NULL;
static esp_timer_handle_t        s_timer = NULL;

// Read battery voltage in mV (after voltage divider compensation)
static int battery_read_mv(void)
{
    int raw = 0;
    adc_oneshot_read(s_adc, BAT_ADC_CHAN, &raw);
    int mv = 0;
    adc_cali_raw_to_voltage(s_cali, raw, &mv);
    // Voltage divider: (62k + 20k) / 20k = 4.1×
    mv = mv * 82 / 20;
    return mv;
}

// Quadratic approximation from Seeed's BSP (maps mV → 0-100%)
static int voltage_to_percent(int mv)
{
    int64_t v = mv;
    int pct = (int)((-1 * v * v + 9016 * v - 19189000) / 10000);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

// Read charging status from IO expander port 0 (active-low pins)
static bool is_charging(void)
{
    uint8_t reg = PCA9535_INPUT0;
    uint8_t val = 0xFF;
    i2c_master_write_read_device(AUDIO_I2C_PORT, IO_EXP_PWR_ADDR,
                                  &reg, 1, &val, 1, pdMS_TO_TICKS(50));
    // Bit 0 (CHRG_DET) low = charging, Bit 1 (STDBY_DET) low = full
    bool charging = !(val & (1 << PWR_CHRG_DET_BIT));
    return charging;
}

static void battery_timer_cb(void *arg)
{
    int mv  = battery_read_mv();
    int pct = voltage_to_percent(mv);
    bool chrg = is_charging();
    ESP_LOGI(TAG, "%d mV → %d%%%s", mv, pct, chrg ? " (charging)" : "");
    display_set_battery(pct, chrg);
}

void battery_init(void)
{
    // ADC1 oneshot
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_ADC_CHAN, &chan_cfg));

    // Calibration (curve fitting on ESP32-S3)
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = BAT_ADC_CHAN,
        .atten    = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali));

    // Configure IO expander port 0 pins 0-2 as inputs for power status
    uint8_t cmd[] = { PCA9535_CONFIG0, 0x07 };  // bits 0-2 = input
    i2c_master_write_to_device(AUDIO_I2C_PORT, IO_EXP_PWR_ADDR,
                                cmd, sizeof(cmd), pdMS_TO_TICKS(100));

    // First reading immediately
    battery_timer_cb(NULL);

    // Periodic timer every 30 s
    esp_timer_create_args_t ta = {
        .callback = battery_timer_cb,
        .name     = "battery",
    };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, 30 * 1000 * 1000));

    ESP_LOGI(TAG, "Battery monitor started (GPIO%d, 30s interval)", BAT_ADC_GPIO);
}
