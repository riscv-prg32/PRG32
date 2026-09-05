#include "prg32.h"

#define CHANNEL_COUNT 8
#define MIN_VOLUME 80
#define MAX_VOLUME 224
#define VOLUME_STEP 16

static uint32_t previous_input;
static uint32_t start_ms;
static uint8_t master_volume;
static uint8_t stereo_width;
static int audio_mode;

static int clamp_pan(int value) {
    if (value < PRG32_AUDIO_PAN_LEFT) return PRG32_AUDIO_PAN_LEFT;
    if (value > PRG32_AUDIO_PAN_RIGHT) return PRG32_AUDIO_PAN_RIGHT;
    return value;
}

static void apply_stereo_width(void) {
    static const int8_t base_pan[CHANNEL_COUNT] = {-8, -46, -16, 28, 42, -32, 34, -64};
    for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
        int pan = (base_pan[channel] * (int)stereo_width) / 100;
        prg32_audio_set_channel_pan((uint8_t)channel, (int8_t)clamp_pan(pan));
    }
}

static void restart_performance(void) {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        prg32_audio_note_off(channel);
    }
    apply_stereo_width();
    prg32_audio_play_track(0);
    start_ms = prg32_ticks_ms();
}

void bach_stereo_init(void) {
    previous_input = 0;
    master_volume = 192;
    stereo_width = 100;
    audio_mode = prg32_audio_get_mode();
    prg32_audio_set_master_volume(master_volume);
    restart_performance();
}

void bach_stereo_update(void) {
    uint32_t input = prg32_input_read();
    uint32_t pressed = input & ~previous_input;

    if (pressed & (PRG32_BTN_A | PRG32_BTN_SELECT)) restart_performance();
    if (pressed & PRG32_BTN_B) {
        prg32_audio_stop_track();
        for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
            prg32_audio_note_off(channel);
        }
    }
    if ((pressed & PRG32_BTN_UP) && master_volume <= MAX_VOLUME - VOLUME_STEP) {
        master_volume += VOLUME_STEP;
        prg32_audio_set_master_volume(master_volume);
    }
    if ((pressed & PRG32_BTN_DOWN) && master_volume >= MIN_VOLUME + VOLUME_STEP) {
        master_volume -= VOLUME_STEP;
        prg32_audio_set_master_volume(master_volume);
    }
    if (pressed & PRG32_BTN_LEFT) {
        stereo_width = stereo_width >= 20 ? (uint8_t)(stereo_width - 20) : 0;
        apply_stereo_width();
    }
    if (pressed & PRG32_BTN_RIGHT) {
        stereo_width = stereo_width <= 80 ? (uint8_t)(stereo_width + 20) : 100;
        apply_stereo_width();
    }
    previous_input = input;
}

static void draw_number(int x, int y, unsigned value, uint16_t color) {
    char text[4];
    int pos = 0;
    if (value >= 100) text[pos++] = (char)('0' + (value / 100) % 10);
    if (value >= 10) text[pos++] = (char)('0' + (value / 10) % 10);
    text[pos++] = (char)('0' + value % 10);
    text[pos] = '\0';
    prg32_gfx_text8(x, y, text, color, PRG32_COLOR_BLACK);
}

void bach_stereo_draw(void) {
    uint32_t elapsed = (prg32_ticks_ms() - start_ms) / 125u;
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "BACH / BWV 846", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 24, "8-VOICE SID-LIKE PRELUDE", PRG32_COLOR_CYAN, 0);
    prg32_gfx_text8(8, 44,
        audio_mode == PRG32_AUDIO_MODE_STEREO ? "AUDIO PLUS: STEREO" : "AUDIO: MONO FOLD-DOWN",
        audio_mode == PRG32_AUDIO_MODE_STEREO ? PRG32_COLOR_GREEN : PRG32_COLOR_YELLOW, 0);

    prg32_gfx_text8(8, 64, "VOLUME", PRG32_COLOR_WHITE, 0);
    draw_number(72, 64, master_volume, PRG32_COLOR_GREEN);
    prg32_gfx_text8(144, 64, "WIDTH", PRG32_COLOR_WHITE, 0);
    draw_number(200, 64, stereo_width, PRG32_COLOR_MAGENTA);

    for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
        int phase = (int)((elapsed * (uint32_t)(channel + 3) + (uint32_t)channel * 11u) % 48u);
        int height = 18 + (phase < 24 ? phase : 48 - phase) * 2;
        int x = 18 + channel * 37;
        uint16_t color = channel < 4 ? PRG32_COLOR_BLUE :
                         channel < 7 ? PRG32_COLOR_MAGENTA : PRG32_COLOR_YELLOW;
        prg32_gfx_rect(x, 164 - height, 24, height, color);
        prg32_gfx_rect(x + 4, 164 - height + 4, 16, 4, PRG32_COLOR_WHITE);
        draw_number(x + 8, 172, (unsigned)(channel + 1), PRG32_COLOR_WHITE);
    }

    prg32_gfx_text8(8, 188, "A RESTART  B RELEASE  <> WIDTH", PRG32_COLOR_WHITE, 0);
}
