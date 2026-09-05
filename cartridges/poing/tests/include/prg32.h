#ifndef PRG32_H
#define PRG32_H
#include <stdint.h>
#define PRG32_BTN_LEFT (1u << 0)
#define PRG32_BTN_RIGHT (1u << 1)
#define PRG32_BTN_UP (1u << 2)
#define PRG32_BTN_DOWN (1u << 3)
#define PRG32_BTN_A (1u << 4)
#define PRG32_BTN_B (1u << 5)
#define PRG32_BTN_SELECT (1u << 6)
uint32_t prg32_input_read(void);
void prg32_gfx_clear(uint16_t color);
void prg32_gfx_pixel(int x, int y, uint16_t color);
void prg32_gfx_rect(int x, int y, int w, int h, uint16_t color);
void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void prg32_audio_note(uint8_t midi_note, uint16_t duration_ms);
#endif
