#include "prg32.h"
#if __has_include("freertos/FreeRTOS.h")
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(int ticks) {
    (void)ticks;
}
#endif
#include <string.h>

#define KEY_COLS 10
#define KEY_ROWS 4
#define KEY_COUNT (KEY_COLS * KEY_ROWS)

static const char g_keys[KEY_COUNT] =
    "ABCDEFGHIJ"
    "KLMNOPQRST"
    "UVWXYZ0123"
    "456789 .<-";

static int key_is_new(uint32_t input, uint32_t last, uint32_t mask) {
    return (input & mask) && !(last & mask);
}

void prg32_keyboard_init(prg32_keyboard_t *keyboard,
                         char *buffer,
                         size_t capacity) {
    if (!keyboard) {
        return;
    }
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->buffer = buffer;
    keyboard->capacity = capacity;
    if (buffer && capacity > 0) {
        buffer[0] = '\0';
    }
}

int prg32_keyboard_update(prg32_keyboard_t *keyboard, uint32_t input_mask) {
    if (!keyboard || !keyboard->buffer || keyboard->capacity == 0) {
        return 0;
    }

    uint32_t last = keyboard->last_input;
    if (key_is_new(input_mask, last, PRG32_BTN_LEFT) && keyboard->cursor > 0) {
        keyboard->cursor--;
    }
    if (key_is_new(input_mask, last, PRG32_BTN_RIGHT) &&
        keyboard->cursor + 1 < KEY_COUNT) {
        keyboard->cursor++;
    }
    if (key_is_new(input_mask, last, PRG32_BTN_UP) &&
        keyboard->cursor >= KEY_COLS) {
        keyboard->cursor -= KEY_COLS;
    }
    if (key_is_new(input_mask, last, PRG32_BTN_DOWN) &&
        keyboard->cursor + KEY_COLS < KEY_COUNT) {
        keyboard->cursor += KEY_COLS;
    }

    int result = 0;
    if (key_is_new(input_mask, last, PRG32_BTN_B)) {
        if (keyboard->length > 0) {
            keyboard->length--;
            keyboard->buffer[keyboard->length] = '\0';
        }
        result = '\b';
    }
    if (key_is_new(input_mask, last, PRG32_BTN_START)) {
        keyboard->done = 1;
        result = -1;
    }
    if (key_is_new(input_mask, last, PRG32_BTN_A)) {
        char ch = g_keys[keyboard->cursor];
        if (ch == '<') {
            if (keyboard->length > 0) {
                keyboard->length--;
                keyboard->buffer[keyboard->length] = '\0';
            }
            result = '\b';
        } else if (ch == '-') {
            keyboard->done = 1;
            result = -1;
        } else if (keyboard->length + 1 < keyboard->capacity) {
            keyboard->buffer[keyboard->length++] = ch == ' ' ? ' ' : ch;
            keyboard->buffer[keyboard->length] = '\0';
            result = (int)ch;
        }
    }

    keyboard->last_input = input_mask;
    return result;
}

void prg32_keyboard_draw(const prg32_keyboard_t *keyboard, int x, int y) {
    if (!keyboard) {
        return;
    }

    prg32_gfx_text8(x, y, keyboard->buffer ? keyboard->buffer : "", 0xffff, 0);
    int top = y + 16;
    for (int row = 0; row < KEY_ROWS; ++row) {
        for (int col = 0; col < KEY_COLS; ++col) {
            int index = row * KEY_COLS + col;
            int px = x + col * 24;
            int py = top + row * 18;
            uint16_t bg = index == keyboard->cursor ? 0x07e0 : 0x001f;
            uint16_t fg = index == keyboard->cursor ? 0x0000 : 0xffff;
            char label[2] = {g_keys[index], '\0'};
            if (g_keys[index] == '<') {
                label[0] = 'B';
            }
            if (g_keys[index] == '-') {
                label[0] = 'O';
            }
            prg32_gfx_rect(px, py, 20, 14, bg);
            prg32_gfx_text8(px + 6, py + 3, label, fg, bg);
        }
    }
}

int prg32_text_input(char *buffer, size_t capacity, const char *title) {
    prg32_keyboard_t keyboard;
    prg32_keyboard_init(&keyboard, buffer, capacity);

    while (!keyboard.done) {
        uint32_t input = prg32_input_read();
        prg32_keyboard_update(&keyboard, input);
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, title ? title : "TEXT INPUT", 0xffff, 0);
        prg32_keyboard_draw(&keyboard, 8, 28);
        prg32_gfx_text8(8, 116, "A SELECT  B DEL  START OK", 0xffff, 0);
        prg32_gfx_present();
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    return (int)keyboard.length;
}
