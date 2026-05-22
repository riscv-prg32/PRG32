#include "prg32.h"

static char text[24];
static prg32_keyboard_t keyboard;

void keyboard_input_c_init(void) {
    prg32_keyboard_init(&keyboard, text, sizeof(text));
}

void keyboard_input_c_update(void) {
    if (!keyboard.done) {
        prg32_keyboard_update(&keyboard, prg32_input_read_menu());
    }
}

void keyboard_input_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "JOYSTICK KEYBOARD", PRG32_COLOR_WHITE, 0);
    prg32_keyboard_draw(&keyboard, 8, 24);
    prg32_gfx_text8(8, 150, "D-PAD MOVE  SELECT KEY", PRG32_COLOR_CYAN, 0);
    prg32_gfx_text8(8, 166, "A BACK  B RETURN", PRG32_COLOR_CYAN, 0);
}
