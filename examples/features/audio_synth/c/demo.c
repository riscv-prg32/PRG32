#include "prg32.h"

static uint32_t last_input;
static int8_t drum_pan = PRG32_AUDIO_PAN_LEFT;

void audio_synth_c_init(void) {
    last_input = 0;
    drum_pan = PRG32_AUDIO_PAN_LEFT;
}

void audio_synth_c_update(void) {
    uint32_t input = prg32_input_read();
    uint32_t pressed = input & ~last_input;

    if (pressed & PRG32_BTN_A) {
        prg32_audio_note_on_pan(0, 0, 36, 220, PRG32_AUDIO_PAN_CENTER);
        prg32_audio_note_on_pan(1, 1, 64, 200, -20);
        prg32_audio_note_on_pan(2, 2, 67, 180, 24);
    }
    if (pressed & PRG32_BTN_B) {
        prg32_audio_note_off(0);
        prg32_audio_note_off(1);
        prg32_audio_note_off(2);
    }
    if (pressed & (PRG32_BTN_LEFT | PRG32_BTN_RIGHT)) {
        drum_pan = drum_pan == PRG32_AUDIO_PAN_LEFT
                       ? PRG32_AUDIO_PAN_RIGHT
                       : PRG32_AUDIO_PAN_LEFT;
        prg32_audio_note_on_pan(3, 3, 72, 230, drum_pan);
    }
    if (pressed & PRG32_BTN_SELECT) {
        prg32_audio_play_track(0);
    }
    last_input = input;
}

void audio_synth_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "SID-LIKE STEREO", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 32, "A: BASS LEAD SAW", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 48, "B: RELEASE CHORD", PRG32_COLOR_CYAN, 0);
    prg32_gfx_text8(8, 64, "LEFT/RIGHT: NOISE", PRG32_COLOR_YELLOW, 0);
    prg32_gfx_text8(8, 80, "SELECT: TRACKER", PRG32_COLOR_MAGENTA, 0);
}
