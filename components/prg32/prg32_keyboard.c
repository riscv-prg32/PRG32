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
#include <stdio.h>
#include <string.h>

#define QWERTY_PAGE 0
#define ASCII_PAGE 1
#define ASCII_FIRST 32
#define ASCII_LAST 126
#define ASCII_COUNT (ASCII_LAST - ASCII_FIRST + 1)
#define ASCII_COMMAND_COUNT 4
#ifdef __ELF__
#define PRG32_FLASH_RODATA __attribute__((section(".rodata")))
#else
#define PRG32_FLASH_RODATA
#endif

typedef enum {
    KEY_CHAR,
    KEY_SPACE,
    KEY_DELETE,
    KEY_SHIFT,
    KEY_RETURN,
    KEY_ASCII,
    KEY_QWERTY,
} key_type_t;

typedef struct {
    const char *normal;
    const char *shifted;
    char normal_ch;
    char shifted_ch;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    key_type_t type;
} qwerty_key_t;

typedef struct {
    char label[8];
    char ch;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    key_type_t type;
} key_view_t;

static const qwerty_key_t qwerty_keys[] PRG32_FLASH_RODATA = {
    {"q", "Q", 'q', 'Q', 4, 28, 28, KEY_CHAR},
    {"w", "W", 'w', 'W', 34, 28, 28, KEY_CHAR},
    {"e", "E", 'e', 'E', 64, 28, 28, KEY_CHAR},
    {"r", "R", 'r', 'R', 94, 28, 28, KEY_CHAR},
    {"t", "T", 't', 'T', 124, 28, 28, KEY_CHAR},
    {"y", "Y", 'y', 'Y', 154, 28, 28, KEY_CHAR},
    {"u", "U", 'u', 'U', 184, 28, 28, KEY_CHAR},
    {"i", "I", 'i', 'I', 214, 28, 28, KEY_CHAR},
    {"o", "O", 'o', 'O', 244, 28, 28, KEY_CHAR},
    {"p", "P", 'p', 'P', 274, 28, 28, KEY_CHAR},

    {"a", "A", 'a', 'A', 18, 50, 28, KEY_CHAR},
    {"s", "S", 's', 'S', 48, 50, 28, KEY_CHAR},
    {"d", "D", 'd', 'D', 78, 50, 28, KEY_CHAR},
    {"f", "F", 'f', 'F', 108, 50, 28, KEY_CHAR},
    {"g", "G", 'g', 'G', 138, 50, 28, KEY_CHAR},
    {"h", "H", 'h', 'H', 168, 50, 28, KEY_CHAR},
    {"j", "J", 'j', 'J', 198, 50, 28, KEY_CHAR},
    {"k", "K", 'k', 'K', 228, 50, 28, KEY_CHAR},
    {"l", "L", 'l', 'L', 258, 50, 28, KEY_CHAR},

    {"shift", "SHIFT", 0, 0, 4, 72, 58, KEY_SHIFT},
    {"z", "Z", 'z', 'Z', 64, 72, 26, KEY_CHAR},
    {"x", "X", 'x', 'X', 92, 72, 26, KEY_CHAR},
    {"c", "C", 'c', 'C', 120, 72, 26, KEY_CHAR},
    {"v", "V", 'v', 'V', 148, 72, 26, KEY_CHAR},
    {"b", "B", 'b', 'B', 176, 72, 26, KEY_CHAR},
    {"n", "N", 'n', 'N', 204, 72, 26, KEY_CHAR},
    {"m", "M", 'm', 'M', 232, 72, 26, KEY_CHAR},
    {"delete", "DELETE", 0, 0, 260, 72, 56, KEY_DELETE},

    {"1", "!", '1', '!', 4, 94, 28, KEY_CHAR},
    {"2", "@", '2', '@', 34, 94, 28, KEY_CHAR},
    {"3", "#", '3', '#', 64, 94, 28, KEY_CHAR},
    {"4", "$", '4', '$', 94, 94, 28, KEY_CHAR},
    {"5", "%", '5', '%', 124, 94, 28, KEY_CHAR},
    {"6", "^", '6', '^', 154, 94, 28, KEY_CHAR},
    {"7", "&", '7', '&', 184, 94, 28, KEY_CHAR},
    {"8", "*", '8', '*', 214, 94, 28, KEY_CHAR},
    {"9", "(", '9', '(', 244, 94, 28, KEY_CHAR},
    {"0", ")", '0', ')', 274, 94, 28, KEY_CHAR},

    {"space", "SPACE", ' ', ' ', 4, 116, 70, KEY_SPACE},
    {"ascii", "ASCII", 0, 0, 78, 116, 62, KEY_ASCII},
    {"return", "RETURN", 0, 0, 144, 116, 76, KEY_RETURN},
};

static int key_is_new(uint32_t input, uint32_t last, uint32_t mask) {
    return (input & mask) && !(last & mask);
}

static int qwerty_count(void) {
    return (int)(sizeof(qwerty_keys) / sizeof(qwerty_keys[0]));
}

static int page_key_count(const prg32_keyboard_t *keyboard) {
    return keyboard && keyboard->page == ASCII_PAGE
        ? ASCII_COUNT + ASCII_COMMAND_COUNT
        : qwerty_count();
}

static void get_qwerty_key(const prg32_keyboard_t *keyboard,
                           int index,
                           key_view_t *out) {
    const qwerty_key_t *key = &qwerty_keys[index];
    memset(out, 0, sizeof(*out));
    snprintf(out->label,
             sizeof(out->label),
             "%s",
             keyboard && keyboard->shift ? key->shifted : key->normal);
    out->ch = keyboard && keyboard->shift ? key->shifted_ch : key->normal_ch;
    out->x = key->x;
    out->y = key->y;
    out->w = key->w;
    out->type = key->type;
}

static void get_ascii_key(int index, key_view_t *out) {
    memset(out, 0, sizeof(*out));
    if (index < ASCII_COUNT) {
        char ch = (char)(ASCII_FIRST + index);
        out->ch = ch;
        out->x = 4 + (uint16_t)(index % 16) * 19;
        out->y = 24 + (uint16_t)(index / 16) * 17;
        out->w = 17;
        out->type = KEY_CHAR;
        if (ch == ' ') {
            snprintf(out->label, sizeof(out->label), "sp");
        } else {
            out->label[0] = ch;
            out->label[1] = '\0';
        }
        return;
    }

    int command = index - ASCII_COUNT;
    if (command == 0) {
        snprintf(out->label, sizeof(out->label), "delete");
        out->x = 4;
        out->y = 130;
        out->w = 56;
        out->type = KEY_DELETE;
    } else if (command == 1) {
        snprintf(out->label, sizeof(out->label), "shift");
        out->x = 64;
        out->y = 130;
        out->w = 52;
        out->type = KEY_SHIFT;
    } else if (command == 2) {
        snprintf(out->label, sizeof(out->label), "qwerty");
        out->x = 120;
        out->y = 130;
        out->w = 60;
        out->type = KEY_QWERTY;
    } else {
        snprintf(out->label, sizeof(out->label), "return");
        out->x = 184;
        out->y = 130;
        out->w = 76;
        out->type = KEY_RETURN;
    }
}

static void get_key(const prg32_keyboard_t *keyboard, int index, key_view_t *out) {
    if (keyboard && keyboard->page == ASCII_PAGE) {
        get_ascii_key(index, out);
    } else {
        get_qwerty_key(keyboard, index, out);
    }
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
        buffer[capacity - 1] = '\0';
        keyboard->length = strlen(buffer);
    }
}

static int key_center_x(const prg32_keyboard_t *keyboard, int index) {
    key_view_t key;
    get_key(keyboard, index, &key);
    return key.x + key.w / 2;
}

static int key_top_y(const prg32_keyboard_t *keyboard, int index) {
    key_view_t key;
    get_key(keyboard, index, &key);
    return key.y;
}

static int iabs(int value) {
    return value < 0 ? -value : value;
}

static int same_row(int a, int b) {
    return iabs(a - b) <= 2;
}

static int find_vertical_row(const prg32_keyboard_t *keyboard,
                             int current_y,
                             int dy) {
    int count = page_key_count(keyboard);
    int row_y = -1;
    for (int i = 0; i < count; ++i) {
        int y = key_top_y(keyboard, i);
        if (dy < 0 && y < current_y) {
            if (row_y < 0 || y > row_y) {
                row_y = y;
            }
        }
        if (dy > 0 && y > current_y) {
            if (row_y < 0 || y < row_y) {
                row_y = y;
            }
        }
    }
    if (row_y >= 0) {
        return row_y;
    }

    for (int i = 0; i < count; ++i) {
        int y = key_top_y(keyboard, i);
        if (row_y < 0 || (dy < 0 ? y > row_y : y < row_y)) {
            row_y = y;
        }
    }
    return row_y;
}

static void move_cursor_horizontal(prg32_keyboard_t *keyboard, int dx) {
    int count = page_key_count(keyboard);
    int current = keyboard->cursor;
    int cx = key_center_x(keyboard, current);
    int row_y = key_top_y(keyboard, current);
    int best = -1;
    int best_distance = 1000000;
    int wrap = -1;
    int wrap_x = dx < 0 ? -1000000 : 1000000;

    for (int i = 0; i < count; ++i) {
        if (i == current || !same_row(key_top_y(keyboard, i), row_y)) {
            continue;
        }
        int tx = key_center_x(keyboard, i);
        if (dx < 0) {
            if (tx < cx && cx - tx < best_distance) {
                best = i;
                best_distance = cx - tx;
            }
            if (tx > wrap_x) {
                wrap = i;
                wrap_x = tx;
            }
        } else {
            if (tx > cx && tx - cx < best_distance) {
                best = i;
                best_distance = tx - cx;
            }
            if (tx < wrap_x) {
                wrap = i;
                wrap_x = tx;
            }
        }
    }

    if (best >= 0) {
        keyboard->cursor = (uint8_t)best;
    } else if (wrap >= 0) {
        keyboard->cursor = (uint8_t)wrap;
    }
}

static void move_cursor_vertical(prg32_keyboard_t *keyboard, int dy) {
    int count = page_key_count(keyboard);
    int current = keyboard->cursor;
    int cx = key_center_x(keyboard, current);
    int target_y = find_vertical_row(keyboard, key_top_y(keyboard, current), dy);
    int best = current;
    int best_distance = 1000000;

    for (int i = 0; i < count; ++i) {
        if (!same_row(key_top_y(keyboard, i), target_y)) {
            continue;
        }
        int distance = iabs(key_center_x(keyboard, i) - cx);
        if (distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    keyboard->cursor = (uint8_t)best;
}

static void move_cursor(prg32_keyboard_t *keyboard, int dx, int dy) {
    int count = page_key_count(keyboard);
    if (keyboard->cursor >= count) {
        keyboard->cursor = 0;
    }
    if (dx < 0 || dx > 0) {
        move_cursor_horizontal(keyboard, dx);
    } else if (dy < 0 || dy > 0) {
        move_cursor_vertical(keyboard, dy);
    }
}

static int insert_char(prg32_keyboard_t *keyboard, char ch) {
    if (keyboard->length + 1 >= keyboard->capacity) {
        return 0;
    }
    keyboard->buffer[keyboard->length++] = ch;
    keyboard->buffer[keyboard->length] = '\0';
    return (int)ch;
}

static int activate_key(prg32_keyboard_t *keyboard) {
    key_view_t key;
    get_key(keyboard, keyboard->cursor, &key);
    if (key.type == KEY_DELETE) {
        if (keyboard->length > 0) {
            keyboard->length--;
            keyboard->buffer[keyboard->length] = '\0';
        }
        return '\b';
    }
    if (key.type == KEY_SHIFT) {
        keyboard->shift = keyboard->shift ? 0 : 1;
        return 0;
    }
    if (key.type == KEY_ASCII) {
        keyboard->page = ASCII_PAGE;
        keyboard->cursor = 0;
        return 0;
    }
    if (key.type == KEY_QWERTY) {
        keyboard->page = QWERTY_PAGE;
        keyboard->cursor = 0;
        return 0;
    }
    if (key.type == KEY_RETURN) {
        keyboard->done = 1;
        return -1;
    }
    if (key.type == KEY_SPACE) {
        return insert_char(keyboard, ' ');
    }
    return insert_char(keyboard, key.ch);
}

int prg32_keyboard_update(prg32_keyboard_t *keyboard, uint32_t input_mask) {
    if (!keyboard || !keyboard->buffer || keyboard->capacity == 0) {
        return 0;
    }

    uint32_t last = keyboard->last_input;
    if (key_is_new(input_mask, last, PRG32_BTN_LEFT)) {
        move_cursor(keyboard, -1, 0);
    }
    if (key_is_new(input_mask, last, PRG32_BTN_RIGHT)) {
        move_cursor(keyboard, 1, 0);
    }
    if (key_is_new(input_mask, last, PRG32_BTN_UP)) {
        move_cursor(keyboard, 0, -1);
    }
    if (key_is_new(input_mask, last, PRG32_BTN_DOWN)) {
        move_cursor(keyboard, 0, 1);
    }

    int result = 0;
    if (key_is_new(input_mask, last, PRG32_BTN_A)) {
        keyboard->cancelled = 1;
        keyboard->done = 1;
        result = -2;
    }
    if (key_is_new(input_mask, last, PRG32_BTN_B)) {
        keyboard->done = 1;
        result = -1;
    }
    if (key_is_new(input_mask, last, PRG32_BTN_SELECT)) {
        result = activate_key(keyboard);
    }

    keyboard->last_input = input_mask;
    return result;
}

void prg32_keyboard_draw(const prg32_keyboard_t *keyboard, int x, int y) {
    if (!keyboard) {
        return;
    }

    prg32_gfx_text8(x, y, keyboard->buffer ? keyboard->buffer : "", 0xffff, 0);
    int count = page_key_count(keyboard);
    for (int index = 0; index < count; ++index) {
        key_view_t key;
        get_key(keyboard, index, &key);
        int px = x + key.x;
        int py = y + key.y;
        uint16_t bg = index == keyboard->cursor ? 0x07e0 : 0x001f;
        uint16_t fg = index == keyboard->cursor ? 0x0000 : 0xffff;
        if (key.type == KEY_SHIFT && keyboard->shift) {
            bg = index == keyboard->cursor ? 0xffe0 : 0x07ff;
        }
        prg32_gfx_rect(px, py, key.w, 14, bg);
        prg32_gfx_text8(px + 2, py + 3, key.label, fg, bg);
    }
}

int prg32_text_input(char *buffer, size_t capacity, const char *title) {
    prg32_keyboard_t keyboard;
    prg32_keyboard_init(&keyboard, buffer, capacity);
    prg32_input_wait_released(PRG32_BTN_LEFT |
                              PRG32_BTN_RIGHT |
                              PRG32_BTN_UP |
                              PRG32_BTN_DOWN |
                              PRG32_BTN_A |
                              PRG32_BTN_B |
                              PRG32_BTN_SELECT);

    while (!keyboard.done) {
        uint32_t input = prg32_input_read_menu();
        prg32_keyboard_update(&keyboard, input);
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, title ? title : "TEXT INPUT", 0xffff, 0);
        prg32_keyboard_draw(&keyboard, 8, 24);
        prg32_gfx_text8(8, 150, "D-PAD MOVE  SELECT KEY", 0xffff, 0);
        prg32_gfx_text8(8, 166, "A BACK  B RETURN", 0x07ff, 0);
        prg32_gfx_present();
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    if (keyboard.cancelled) {
        prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);
        return -1;
    }
    prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);
    return (int)keyboard.length;
}
