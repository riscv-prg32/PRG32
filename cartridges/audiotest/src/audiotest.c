#include "prg32.h"
#include <stdint.h>
#include <stdbool.h>

static int s_timer = 0;
static int s_note_idx = 0;
static int s_phase = 0; // 0=Left, 1=Right, 2=Center
static bool s_playing = true;
static int s_mode = 0;
static uint32_t s_prev_input = 0;

static const uint8_t s_notes[3] = {60, 64, 67}; // C4, E4, G4

void audiotest_init(void) {
    s_timer = 0;
    s_note_idx = 0;
    s_phase = 0;
    s_playing = true;
    s_mode = prg32_audio_get_mode();
    s_prev_input = 0;
}

void audiotest_update(void) {
    uint32_t input = prg32_input_read();
    uint32_t pressed = input & ~s_prev_input;
    s_prev_input = input;

    if (pressed & PRG32_BTN_A) {
        s_playing = !s_playing;
        if (!s_playing) {
            prg32_audio_note_off(0);
        }
    }

    if (s_playing) {
        s_timer++;
        if (s_timer >= 15) {
            s_timer = 0;

            int pan = 0;
            if (s_phase == 0) pan = PRG32_AUDIO_PAN_LEFT;
            else if (s_phase == 1) pan = PRG32_AUDIO_PAN_RIGHT;
            else if (s_phase == 2) pan = 0; // Center

            prg32_audio_set_channel_pan(0, pan);
            prg32_audio_note(0, PRG32_DEFAULT_INSTRUMENT_ID, s_notes[s_note_idx], 255, 200);

            s_note_idx++;
            if (s_note_idx >= 3) {
                s_note_idx = 0;
                s_phase++;
                if (s_phase >= 3) {
                    s_phase = 0;
                }
            }
        }
    }
}

void audiotest_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "AUDIO PAN TEST", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);

    if (s_mode == 2) { // 2 = PRG32_AUDIO_MODE_STEREO
        prg32_gfx_text8(8, 24, "MODE: STEREO", PRG32_COLOR_GREEN, PRG32_COLOR_BLACK);
    } else {
        prg32_gfx_text8(8, 24, "MODE: MONO", PRG32_COLOR_MAGENTA, PRG32_COLOR_BLACK);
    }

    prg32_gfx_text8(8, 44, "PRESS A TO TOGGLE", PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);

    if (s_playing) {
        if (s_phase == 0) {
            prg32_gfx_text8(8, 64, "PLAYING: LEFT", PRG32_COLOR_RED, PRG32_COLOR_BLACK);
        } else if (s_phase == 1) {
            prg32_gfx_text8(8, 64, "PLAYING: RIGHT", PRG32_COLOR_BLUE, PRG32_COLOR_BLACK);
        } else if (s_phase == 2) {
            prg32_gfx_text8(8, 64, "PLAYING: CENTER", PRG32_COLOR_GREEN, PRG32_COLOR_BLACK);
        }

        // s_note_idx is pointing to the *next* note. So the currently playing note is -1
        int current_playing_idx = (s_note_idx == 0) ? 2 : s_note_idx - 1;

        for (int i = 0; i < 3; i++) {
            uint16_t color = (i == current_playing_idx) ? PRG32_COLOR_WHITE : PRG32_COLOR_BLUE;
            prg32_gfx_rect(16 + i * 40, 90, 32, 32, color);
        }
    } else {
        prg32_gfx_text8(8, 64, "STOPPED", PRG32_COLOR_RED, PRG32_COLOR_BLACK);
    }
}
