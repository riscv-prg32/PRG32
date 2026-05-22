#include "prg32.h"
#include "prg32_config.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>

#ifndef PRG32_PIN_RGB_LED
#define PRG32_PIN_RGB_LED -1
#endif

static rmt_channel_handle_t g_led_channel;
static rmt_encoder_handle_t g_led_encoder;
static int g_led_gpio = -1;
static int g_led_ready;
static int g_audio_vu_enabled;

static int pin_matches(int gpio, int pin) {
    return gpio >= 0 && pin >= 0 && gpio == pin;
}

static int pin_conflicts(int gpio) {
    if (gpio < 0) {
        return 1;
    }
    return pin_matches(gpio, PRG32_PIN_LCD_MOSI) ||
           pin_matches(gpio, PRG32_PIN_LCD_MISO) ||
           pin_matches(gpio, PRG32_PIN_LCD_SCLK) ||
           pin_matches(gpio, PRG32_PIN_LCD_CS) ||
           pin_matches(gpio, PRG32_PIN_LCD_DC) ||
           pin_matches(gpio, PRG32_PIN_LCD_RST) ||
           pin_matches(gpio, PRG32_PIN_LCD_BL) ||
           pin_matches(gpio, PRG32_PIN_BTN_LEFT) ||
           pin_matches(gpio, PRG32_PIN_BTN_RIGHT) ||
           pin_matches(gpio, PRG32_PIN_BTN_UP) ||
           pin_matches(gpio, PRG32_PIN_BTN_DOWN) ||
           pin_matches(gpio, PRG32_PIN_BTN_A) ||
           pin_matches(gpio, PRG32_PIN_BTN_B) ||
           pin_matches(gpio, PRG32_PIN_BTN_SELECT) ||
           pin_matches(gpio, PRG32_PIN_BUZZER);
}

static void led_release(void) {
    if (g_led_channel) {
        rmt_disable(g_led_channel);
        rmt_del_channel(g_led_channel);
    }
    if (g_led_encoder) {
        rmt_del_encoder(g_led_encoder);
    }
    g_led_channel = NULL;
    g_led_encoder = NULL;
    g_led_ready = 0;
    g_led_gpio = -1;
}

int prg32_rgb_led_init(int gpio) {
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
    (void)gpio;
    return -1;
#else
    if (gpio < 0) {
        gpio = PRG32_PIN_RGB_LED;
    }
    if (pin_conflicts(gpio)) {
        return -1;
    }
    if (g_led_ready && g_led_gpio == gpio) {
        return 0;
    }

    led_release();

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    if (rmt_new_tx_channel(&tx_cfg, &g_led_channel) != ESP_OK) {
        led_release();
        return -1;
    }

    rmt_bytes_encoder_config_t encoder_cfg = {
        .bit0 = {
            .duration0 = 3,
            .level0 = 1,
            .duration1 = 9,
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = 9,
            .level0 = 1,
            .duration1 = 3,
            .level1 = 0,
        },
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&encoder_cfg, &g_led_encoder) != ESP_OK ||
        rmt_enable(g_led_channel) != ESP_OK) {
        led_release();
        return -1;
    }

    g_led_gpio = gpio;
    g_led_ready = 1;
    prg32_rgb_led_off();
    return 0;
#endif
}

int prg32_rgb_led_available(void) {
    return g_led_ready;
}

void prg32_rgb_led_set(uint8_t red, uint8_t green, uint8_t blue) {
    if (!g_led_ready || !g_led_channel || !g_led_encoder) {
        return;
    }

    uint8_t grb[3] = { green, red, blue };
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    if (rmt_transmit(g_led_channel,
                     g_led_encoder,
                     grb,
                     sizeof(grb),
                     &tx_config) == ESP_OK) {
        rmt_tx_wait_all_done(g_led_channel, pdMS_TO_TICKS(10));
    }
}

void prg32_rgb_led_off(void) {
    prg32_rgb_led_set(0, 0, 0);
}

void prg32_rgb_led_vu(uint8_t level) {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    if (level < 85) {
        blue = (uint8_t)(255 - level * 3);
        green = (uint8_t)(level * 3);
    } else if (level < 170) {
        uint8_t t = (uint8_t)((level - 85) * 3);
        green = 255;
        red = t;
    } else {
        uint8_t t = (uint8_t)((level - 170) * 3);
        red = 255;
        green = (uint8_t)(255 - t);
    }

    prg32_rgb_led_set(red, green, blue);
}

void prg32_audio_led_vu_enable(int enabled) {
    g_audio_vu_enabled = enabled ? 1 : 0;
    if (!g_audio_vu_enabled) {
        prg32_rgb_led_off();
    }
}

int prg32_audio_led_vu_enabled(void) {
    return g_audio_vu_enabled;
}

void prg32_audio_led_vu_level(uint8_t level) {
    if (g_audio_vu_enabled) {
        prg32_rgb_led_vu(level);
    }
}
