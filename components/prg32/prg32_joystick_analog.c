#include "prg32.h"
#include "prg32_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "prg32_joystick_analog";

#if !CONFIG_PRG32_DISPLAY_QEMU_RGB && defined(CONFIG_PRG32_JOYSTICK_ANALOG) && CONFIG_PRG32_JOYSTICK_ANALOG

#include "esp_adc/adc_continuous.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"

#define ADC_READ_FRAME_BYTES 256

static adc_continuous_handle_t s_adc_handle = NULL;
static int s_chan_x = -1;
static int s_chan_y = -1;
static int s_last_raw_x = 2048;
static int s_last_raw_y = 2048;

void prg32_joystick_analog_init(void) {
    if (PRG32_PIN_JOYSTICK_X < 0 || PRG32_PIN_JOYSTICK_Y < 0) {
        ESP_LOGW(TAG, "Analog joystick pins unconfigured (X=%d, Y=%d)",
                 PRG32_PIN_JOYSTICK_X, PRG32_PIN_JOYSTICK_Y);
        return;
    }

    adc_unit_t unit_x = ADC_UNIT_1;
    adc_channel_t ch_x = ADC_CHANNEL_0;
    adc_unit_t unit_y = ADC_UNIT_1;
    adc_channel_t ch_y = ADC_CHANNEL_1;

    esp_err_t err = adc_continuous_io_to_channel(PRG32_PIN_JOYSTICK_X, &unit_x, &ch_x);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to map GPIO%d to ADC channel: %s",
                 PRG32_PIN_JOYSTICK_X, esp_err_to_name(err));
        return;
    }

    err = adc_continuous_io_to_channel(PRG32_PIN_JOYSTICK_Y, &unit_y, &ch_y);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to map GPIO%d to ADC channel: %s",
                 PRG32_PIN_JOYSTICK_Y, esp_err_to_name(err));
        return;
    }

    s_chan_x = (int)ch_x;
    s_chan_y = (int)ch_y;

    ESP_LOGI(TAG, "Initializing ADC continuous mode for Joystick: X=GPIO%d (ADC%d CH%d), Y=GPIO%d (ADC%d CH%d)",
             PRG32_PIN_JOYSTICK_X, unit_x + 1, ch_x,
             PRG32_PIN_JOYSTICK_Y, unit_y + 1, ch_y);

    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = ADC_READ_FRAME_BYTES,
    };

    err = adc_continuous_new_handle(&adc_handle_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_new_handle failed: %s", esp_err_to_name(err));
        s_adc_handle = NULL;
        return;
    }

    adc_digi_pattern_config_t adc_patterns[2] = {0};
    adc_patterns[0].atten = ADC_ATTEN_DB_12;
    adc_patterns[0].channel = ch_x & 0x7;
    adc_patterns[0].unit = unit_x;
    adc_patterns[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    adc_patterns[1].atten = ADC_ATTEN_DB_12;
    adc_patterns[1].channel = ch_y & 0x7;
    adc_patterns[1].unit = unit_y;
    adc_patterns[1].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 20 * 1000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num = 2,
        .adc_pattern = adc_patterns,
    };

    err = adc_continuous_config(s_adc_handle, &dig_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_config failed: %s", esp_err_to_name(err));
        adc_continuous_deinit(s_adc_handle);
        s_adc_handle = NULL;
        return;
    }

    err = adc_continuous_start(s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_start failed: %s", esp_err_to_name(err));
        adc_continuous_deinit(s_adc_handle);
        s_adc_handle = NULL;
        return;
    }

    ESP_LOGI(TAG, "ADC continuous driver started successfully");
}

uint32_t prg32_joystick_analog_read(void) {
    if (!s_adc_handle) {
        return 0;
    }

    uint8_t buffer[ADC_READ_FRAME_BYTES];
    uint32_t ret_num = 0;
    esp_err_t ret = adc_continuous_read(s_adc_handle, buffer, sizeof(buffer), &ret_num, 0);
    if (ret == ESP_OK && ret_num > 0) {
        for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buffer[i];
            uint32_t chan = p->type2.channel;
            uint32_t data = p->type2.data;
            if ((int)chan == s_chan_x) {
                s_last_raw_x = (int)data;
            } else if ((int)chan == s_chan_y) {
                s_last_raw_y = (int)data;
            }
        }
    }

    uint32_t mask = 0;

    // Horizontal axis (X): triggers LEFT or RIGHT
    if (PRG32_JOYSTICK_ADC_LEFT_THRESHOLD <= PRG32_JOYSTICK_ADC_RIGHT_THRESHOLD) {
        if (s_last_raw_x < PRG32_JOYSTICK_ADC_LEFT_THRESHOLD) {
            mask |= PRG32_BTN_LEFT;
        } else if (s_last_raw_x > PRG32_JOYSTICK_ADC_RIGHT_THRESHOLD) {
            mask |= PRG32_BTN_RIGHT;
        }
    } else {
        // Inverted X-axis orientation
        if (s_last_raw_x > PRG32_JOYSTICK_ADC_LEFT_THRESHOLD) {
            mask |= PRG32_BTN_LEFT;
        } else if (s_last_raw_x < PRG32_JOYSTICK_ADC_RIGHT_THRESHOLD) {
            mask |= PRG32_BTN_RIGHT;
        }
    }

    // Vertical axis (Y): triggers UP or DOWN
    if (PRG32_JOYSTICK_ADC_UP_THRESHOLD <= PRG32_JOYSTICK_ADC_DOWN_THRESHOLD) {
        if (s_last_raw_y < PRG32_JOYSTICK_ADC_UP_THRESHOLD) {
            mask |= PRG32_BTN_UP;
        } else if (s_last_raw_y > PRG32_JOYSTICK_ADC_DOWN_THRESHOLD) {
            mask |= PRG32_BTN_DOWN;
        }
    } else {
        // Inverted Y-axis orientation
        if (s_last_raw_y > PRG32_JOYSTICK_ADC_UP_THRESHOLD) {
            mask |= PRG32_BTN_UP;
        } else if (s_last_raw_y < PRG32_JOYSTICK_ADC_DOWN_THRESHOLD) {
            mask |= PRG32_BTN_DOWN;
        }
    }

    return mask;
}

int prg32_joystick_analog_get_raw(int *out_x, int *out_y) {
    if (out_x) {
        *out_x = s_last_raw_x;
    }
    if (out_y) {
        *out_y = s_last_raw_y;
    }
    return 0;
}

#else

void prg32_joystick_analog_init(void) {
}

uint32_t prg32_joystick_analog_read(void) {
    return 0;
}

int prg32_joystick_analog_get_raw(int *out_x, int *out_y) {
    if (out_x) {
        *out_x = 2048;
    }
    if (out_y) {
        *out_y = 2048;
    }
    return -1;
}

#endif
