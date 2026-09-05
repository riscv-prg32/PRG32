#ifndef TEST_PRG32_H
#define TEST_PRG32_H
#include <stdint.h>
#define PRG32_AUDIO_PAN_LEFT (-64)
#define PRG32_AUDIO_PAN_RIGHT 63
#define PRG32_AUDIO_MODE_STEREO 1
#define PRG32_BTN_UP (1u << 0)
#define PRG32_BTN_DOWN (1u << 1)
#define PRG32_BTN_LEFT (1u << 2)
#define PRG32_BTN_RIGHT (1u << 3)
#define PRG32_BTN_SELECT (1u << 4)
#define PRG32_BTN_A (1u << 5)
#define PRG32_BTN_B (1u << 6)
#define PRG32_COLOR_BLACK 0x0000
#define PRG32_COLOR_WHITE 0xffff
#define PRG32_COLOR_GREEN 0x07e0
#define PRG32_COLOR_YELLOW 0xffe0
#define PRG32_COLOR_CYAN 0x07ff
#define PRG32_COLOR_BLUE 0x001f
#define PRG32_COLOR_MAGENTA 0xf81f
uint32_t prg32_input_read(void);
uint32_t prg32_ticks_ms(void);
int prg32_audio_get_mode(void);
void prg32_audio_set_master_volume(uint8_t volume);
void prg32_audio_set_channel_pan(uint8_t channel, int8_t pan);
void prg32_audio_note_off(uint8_t channel);
void prg32_audio_play_track(uint8_t track);
void prg32_audio_stop_track(void);
void prg32_gfx_clear(uint16_t color);
void prg32_gfx_text8(int x, int y, const char *text, uint16_t foreground, uint16_t background);
void prg32_gfx_rect(int x, int y, int width, int height, uint16_t color);
#endif
