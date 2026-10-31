#include "prg32.h"
#include "prg32_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

#if !CONFIG_PRG32_DISPLAY_QEMU_RGB && defined(CONFIG_PRG32_JOYSTICK_ANALOG) && CONFIG_PRG32_JOYSTICK_ANALOG

static const char *TAG = "prg32_joystick_analog";

#include "esp_adc/adc_oneshot.h"

/* The stick is sampled on demand: each prg32_joystick_analog_read() performs
 * one conversion per axis. One-shot mode uses no DMA and no interrupt, so it
 * keeps working across flash writes (cartridge uploads, Store downloads, NVS
 * saves). The earlier continuous DMA mode stopped for good after the first
 * flash erase, because its interrupt cannot run while the flash cache is off.
 */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_channel_t s_chan_x;
static adc_channel_t s_chan_y;
static int s_last_raw_x = 2048;
static int s_last_raw_y = 2048;
/* Rest position measured at boot. A KY-023 rests near 1650, not at the
 * 2048 midpoint, so the axes are centred on what the stick really reads. */
static int s_center_x = 2048;
static int s_center_y = 2048;

#define ADC_RAW_MAX 4095
#define AXIS_FULL 127
#define CENTER_SAMPLES 16
/* A rest reading outside this window means the stick was held at boot. */
#define CENTER_MIN 512
#define CENTER_MAX 3584

static void sample_axes(void);

/* Average a few readings with the stick released to find its rest position. */
static void calibrate_center(void) {
    int sum_x = 0;
    int sum_y = 0;
    for (int i = 0; i < CENTER_SAMPLES; i++) {
        sample_axes();
        sum_x += s_last_raw_x;
        sum_y += s_last_raw_y;
    }
    s_center_x = sum_x / CENTER_SAMPLES;
    s_center_y = sum_y / CENTER_SAMPLES;
    if (s_center_x < CENTER_MIN || s_center_x > CENTER_MAX ||
        s_center_y < CENTER_MIN || s_center_y > CENTER_MAX) {
        ESP_LOGW(TAG, "Stick not at rest during boot (X=%d Y=%d); using 2048",
                 s_center_x, s_center_y);
        s_center_x = 2048;
        s_center_y = 2048;
    }
}

void prg32_joystick_analog_init(void) {
    if (PRG32_PIN_JOYSTICK_X < 0 || PRG32_PIN_JOYSTICK_Y < 0) {
        ESP_LOGW(TAG, "Analog joystick pins unconfigured (X=%d, Y=%d)",
                 PRG32_PIN_JOYSTICK_X, PRG32_PIN_JOYSTICK_Y);
        return;
    }

    adc_unit_t unit_x;
    adc_unit_t unit_y;
    esp_err_t err = adc_oneshot_io_to_channel(PRG32_PIN_JOYSTICK_X, &unit_x, &s_chan_x);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to map GPIO%d to ADC channel: %s",
                 PRG32_PIN_JOYSTICK_X, esp_err_to_name(err));
        return;
    }

    err = adc_oneshot_io_to_channel(PRG32_PIN_JOYSTICK_Y, &unit_y, &s_chan_y);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to map GPIO%d to ADC channel: %s",
                 PRG32_PIN_JOYSTICK_Y, esp_err_to_name(err));
        return;
    }

    if (unit_x != ADC_UNIT_1 || unit_y != ADC_UNIT_1) {
        ESP_LOGE(TAG, "Joystick pins must be ADC1 inputs (X=GPIO%d, Y=GPIO%d)",
                 PRG32_PIN_JOYSTICK_X, PRG32_PIN_JOYSTICK_Y);
        return;
    }

    ESP_LOGI(TAG, "Initializing ADC one-shot mode for Joystick: X=GPIO%d (ADC1 CH%d), Y=GPIO%d (ADC1 CH%d)",
             PRG32_PIN_JOYSTICK_X, s_chan_x, PRG32_PIN_JOYSTICK_Y, s_chan_y);

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        s_adc_handle = NULL;
        return;
    }

    /* 12 dB attenuation covers the full 0 V to 3.3 V potentiometer swing. */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_chan_x, &chan_cfg);
    if (err == ESP_OK) {
        err = adc_oneshot_config_channel(s_adc_handle, s_chan_y, &chan_cfg);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return;
    }

    calibrate_center();
    ESP_LOGI(TAG, "ADC one-shot driver ready, rest position X=%d Y=%d",
             s_center_x, s_center_y);
}

/* Convert both axes now. A failed read (for example while another task holds
 * ADC1) keeps the previous value, so the stick never jumps to a false
 * direction. */
static void sample_axes(void) {
    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, s_chan_x, &raw) == ESP_OK) {
        s_last_raw_x = raw;
    }
    if (adc_oneshot_read(s_adc_handle, s_chan_y, &raw) == ESP_OK) {
        s_last_raw_y = raw;
    }
}

uint32_t prg32_joystick_analog_read(void) {
    if (!s_adc_handle) {
        return 0;
    }
    sample_axes();

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

/* Fresh 12-bit axis values (0 to 4095) for axis-based input. Without a
 * working ADC it reports the centre and returns -1, like the stub below. */
int prg32_joystick_analog_get_raw(int *out_x, int *out_y) {
    if (s_adc_handle) {
        sample_axes();
    }
    if (out_x) {
        *out_x = s_last_raw_x;
    }
    if (out_y) {
        *out_y = s_last_raw_y;
    }
    return s_adc_handle ? 0 : -1;
}

int prg32_joystick_analog_available(void) {
    return s_adc_handle != NULL;
}

/* Map a raw reading to -127..127 around the rest point. Each side is scaled
 * on its own because the rest point is off-centre, then the deadzone is cut
 * out and the rest rescaled so full tilt still reads 127. */
static int scale_axis(int raw, int center) {
    int offset = raw - center;
    int span = offset < 0 ? center : ADC_RAW_MAX - center;
    int value = offset * AXIS_FULL / span;
    int magnitude = value < 0 ? -value : value;
    if (magnitude <= PRG32_JOYSTICK_AXIS_DEADZONE) {
        return 0;
    }
    magnitude = (magnitude - PRG32_JOYSTICK_AXIS_DEADZONE) * AXIS_FULL /
                (AXIS_FULL - PRG32_JOYSTICK_AXIS_DEADZONE);
    if (magnitude > AXIS_FULL) {
        magnitude = AXIS_FULL;
    }
    return value < 0 ? -magnitude : magnitude;
}

/* Fresh axis positions, -127 (left/up) to 127 (right/down). Swapped
 * thresholds mark an inverted axis, the same rule the direction bits use. */
int prg32_joystick_analog_get_axes(int *out_x, int *out_y) {
    int x = 0;
    int y = 0;
    if (s_adc_handle) {
        sample_axes();
        x = scale_axis(s_last_raw_x, s_center_x);
        y = scale_axis(s_last_raw_y, s_center_y);
        if (PRG32_JOYSTICK_ADC_LEFT_THRESHOLD > PRG32_JOYSTICK_ADC_RIGHT_THRESHOLD) {
            x = -x;
        }
        if (PRG32_JOYSTICK_ADC_UP_THRESHOLD > PRG32_JOYSTICK_ADC_DOWN_THRESHOLD) {
            y = -y;
        }
    }
    if (out_x) {
        *out_x = x;
    }
    if (out_y) {
        *out_y = y;
    }
    return s_adc_handle ? 0 : -1;
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

int prg32_joystick_analog_available(void) {
    return 0;
}

int prg32_joystick_analog_get_axes(int *out_x, int *out_y) {
    if (out_x) {
        *out_x = 0;
    }
    if (out_y) {
        *out_y = 0;
    }
    return -1;
}

#endif
