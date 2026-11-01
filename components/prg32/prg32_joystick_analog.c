#include "prg32.h"
#include "prg32_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

#if !CONFIG_PRG32_DISPLAY_QEMU_RGB && defined(CONFIG_PRG32_JOYSTICK_ANALOG) && CONFIG_PRG32_JOYSTICK_ANALOG

static const char *TAG = "prg32_joystick_analog";

#include "esp_adc/adc_oneshot.h"
#include "nvs.h"

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
#define ADC_RAW_MAX 4095
#define AXIS_FULL 127
#define CENTER_SAMPLES 16
/* A rest reading outside this window means the stick was held at boot. */
#define CENTER_MIN 512
#define CENTER_MAX 3584
/* A calibrated side shorter than this means the stick was not moved to the
 * edge, and would turn small tilts into full deflection. */
#define CALIBRATION_MIN_SPAN 400

/* Rest position and travel of each axis. A KY-023 rests near 1650, not at
 * the 2048 midpoint, so the axes are centred on what the stick really reads.
 * The setup menu records these values in flash; without a saved calibration
 * the rest position is measured at boot and the travel is the full ADC range. */
static prg32_joystick_calibration_t s_cal = {
    .center_x = 2048, .center_y = 2048,
    .min_x = 0, .max_x = ADC_RAW_MAX,
    .min_y = 0, .max_y = ADC_RAW_MAX,
};
static int s_cal_saved = 0;

#define CAL_NVS_NAMESPACE "prg32"

static void sample_axes(void);

/* Average a few readings with the stick released to find its rest position. */
static void measure_center(void) {
    int sum_x = 0;
    int sum_y = 0;
    for (int i = 0; i < CENTER_SAMPLES; i++) {
        sample_axes();
        sum_x += s_last_raw_x;
        sum_y += s_last_raw_y;
    }
    s_cal.center_x = sum_x / CENTER_SAMPLES;
    s_cal.center_y = sum_y / CENTER_SAMPLES;
    if (s_cal.center_x < CENTER_MIN || s_cal.center_x > CENTER_MAX ||
        s_cal.center_y < CENTER_MIN || s_cal.center_y > CENTER_MAX) {
        ESP_LOGW(TAG, "Stick not at rest during boot (X=%d Y=%d); using 2048",
                 s_cal.center_x, s_cal.center_y);
        s_cal.center_x = 2048;
        s_cal.center_y = 2048;
    }
}

static int axis_calibration_valid(int min, int center, int max) {
    return min >= 0 && max <= ADC_RAW_MAX &&
           center - min >= CALIBRATION_MIN_SPAN &&
           max - center >= CALIBRATION_MIN_SPAN;
}

static int calibration_valid(const prg32_joystick_calibration_t *cal) {
    return axis_calibration_valid(cal->min_x, cal->center_x, cal->max_x) &&
           axis_calibration_valid(cal->min_y, cal->center_y, cal->max_y);
}

/* Read a saved calibration; every key must be present and the values valid. */
static int load_calibration(void) {
    nvs_handle_t nvs;
    if (nvs_open(CAL_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return 0;
    }
    uint16_t v[6];
    const char *keys[6] = {"joy_cx", "joy_cy", "joy_xmin",
                           "joy_xmax", "joy_ymin", "joy_ymax"};
    int found = 1;
    for (int i = 0; i < 6; i++) {
        if (nvs_get_u16(nvs, keys[i], &v[i]) != ESP_OK) {
            found = 0;
        }
    }
    nvs_close(nvs);
    if (!found) {
        return 0;
    }
    prg32_joystick_calibration_t cal = {
        .center_x = v[0], .center_y = v[1],
        .min_x = v[2], .max_x = v[3],
        .min_y = v[4], .max_y = v[5],
    };
    if (!calibration_valid(&cal)) {
        ESP_LOGW(TAG, "Ignoring invalid saved joystick calibration");
        return 0;
    }
    s_cal = cal;
    return 1;
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

    s_cal_saved = load_calibration();
    if (!s_cal_saved) {
        measure_center();
    }
    ESP_LOGI(TAG, "ADC one-shot driver ready, rest position X=%d Y=%d (%s)",
             s_cal.center_x, s_cal.center_y,
             s_cal_saved ? "saved calibration" : "measured at boot");
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

/* A direction stays on until the tilt falls below three fifths of the
 * threshold, so a stick held near the threshold does not flicker. */
#define DIRECTION_RELEASE (PRG32_JOYSTICK_DIRECTION_THRESHOLD * 3 / 5)

static uint32_t s_directions_held = 0;

/* Turn one direction on or off from its tilt (0..127 toward that side). */
static uint32_t direction_bit(uint32_t bit, int tilt) {
    int limit = (s_directions_held & bit) ? DIRECTION_RELEASE
                                          : PRG32_JOYSTICK_DIRECTION_THRESHOLD;
    return tilt >= limit ? bit : 0;
}

/* Direction bits from the calibrated axes, so every direction turns on at
 * the same tilt whatever the stick's rest position. Diagonals set two bits. */
uint32_t prg32_joystick_analog_read(void) {
    int x = 0;
    int y = 0;
    if (prg32_joystick_analog_get_axes(&x, &y) != 0) {
        return 0;
    }
    uint32_t mask = 0;
    mask |= direction_bit(PRG32_BTN_LEFT, -x);
    mask |= direction_bit(PRG32_BTN_RIGHT, x);
    mask |= direction_bit(PRG32_BTN_UP, -y);
    mask |= direction_bit(PRG32_BTN_DOWN, y);
    s_directions_held = mask;
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
 * on its own travel because the rest point is off-centre, then the deadzone
 * is cut out and the rest rescaled so full tilt still reads 127. */
static int scale_axis(int raw, int min, int center, int max) {
    int offset = raw - center;
    int span = offset < 0 ? center - min : max - center;
    int value = offset * AXIS_FULL / span;
    int magnitude = value < 0 ? -value : value;
    if (magnitude > AXIS_FULL) {
        magnitude = AXIS_FULL; /* past the calibrated edge */
    }
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

/* Fresh axis positions, -127 (left/up) to 127 (right/down), turned around
 * for a stick mounted the other way (PRG32_JOYSTICK_INVERT_X/_Y). */
int prg32_joystick_analog_get_axes(int *out_x, int *out_y) {
    int x = 0;
    int y = 0;
    if (s_adc_handle) {
        sample_axes();
        x = scale_axis(s_last_raw_x, s_cal.min_x, s_cal.center_x, s_cal.max_x);
        y = scale_axis(s_last_raw_y, s_cal.min_y, s_cal.center_y, s_cal.max_y);
        if (PRG32_JOYSTICK_INVERT_X) {
            x = -x;
        }
        if (PRG32_JOYSTICK_INVERT_Y) {
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

int prg32_joystick_analog_calibration_get(prg32_joystick_calibration_t *out) {
    if (out) {
        *out = s_cal;
    }
    return s_cal_saved;
}

int prg32_joystick_analog_calibration_save(const prg32_joystick_calibration_t *cal) {
    if (!cal || !calibration_valid(cal)) {
        return -1;
    }
    nvs_handle_t nvs;
    if (nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_set_u16(nvs, "joy_cx", (uint16_t)cal->center_x);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "joy_cy", (uint16_t)cal->center_y);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "joy_xmin", (uint16_t)cal->min_x);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "joy_xmax", (uint16_t)cal->max_x);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "joy_ymin", (uint16_t)cal->min_y);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "joy_ymax", (uint16_t)cal->max_y);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving joystick calibration failed: %s", esp_err_to_name(err));
        return -1;
    }
    s_cal = *cal;
    s_cal_saved = 1;
    ESP_LOGI(TAG, "Saved joystick calibration: X %d..%d..%d, Y %d..%d..%d",
             cal->min_x, cal->center_x, cal->max_x,
             cal->min_y, cal->center_y, cal->max_y);
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

int prg32_joystick_analog_calibration_get(prg32_joystick_calibration_t *out) {
    if (out) {
        *out = (prg32_joystick_calibration_t){2048, 2048, 0, 4095, 0, 4095};
    }
    return 0;
}

int prg32_joystick_analog_calibration_save(const prg32_joystick_calibration_t *cal) {
    (void)cal;
    return -1;
}

#endif
