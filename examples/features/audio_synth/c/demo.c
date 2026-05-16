#include "prg32.h"

static const prg32_note_t melody[] = {
    {262, 120},
    {330, 120},
    {392, 120},
    {523, 180},
    {0, 80},
    {392, 120},
    {523, 240},
};

static const uint8_t click_sample[] = {
    128, 220, 250, 220, 128, 40, 8, 40,
    128, 180, 220, 180, 128, 80, 40, 80,
};

static uint32_t last_input;

void audio_synth_c_init(void) {
    last_input = 0;
}

void audio_synth_c_update(void) {
    uint32_t input = prg32_input_read();
    if ((input & PRG32_BTN_A) && !(last_input & PRG32_BTN_A)) {
        prg32_audio_play_notes(melody, sizeof(melody) / sizeof(melody[0]));
    }
    if ((input & PRG32_BTN_B) && !(last_input & PRG32_BTN_B)) {
        prg32_audio_sample_u8(click_sample, sizeof(click_sample), 8000);
    }
    last_input = input;
}

void audio_synth_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "AUDIO SYNTH C", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 32, "A NOTES", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 48, "B SAMPLE", PRG32_COLOR_CYAN, 0);
}
