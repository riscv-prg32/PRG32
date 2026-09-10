#ifndef PRG32_H
#define PRG32_H
#include <stdint.h>
#include <stddef.h>
#define PRG32_BTN_LEFT (1u << 0)
#define PRG32_BTN_RIGHT (1u << 1)
#define PRG32_BTN_UP (1u << 2)
#define PRG32_BTN_DOWN (1u << 3)
#define PRG32_BTN_A (1u << 4)
#define PRG32_BTN_B (1u << 5)
#define PRG32_BTN_SELECT (1u << 6)
#define PRG32_PERF_ABI_VERSION 1u
#define PRG32_PERF_COLOR_INDEXED 2u
typedef struct { uint16_t abi_version,struct_size;uint32_t suite_version;const char *name; } prg32_perf_suite_desc_t;
typedef struct { uint16_t abi_version,struct_size;uint32_t case_index,color_mode;const char *name,*metric_goal; } prg32_perf_case_desc_t;
typedef struct { uint16_t abi_version,struct_size;uint32_t frame_index,update_us,draw_us,present_us,frame_total_us,input_mask; } prg32_perf_sample_t;
uint32_t prg32_input_read(void);
void prg32_gfx_clear(uint16_t color);
void prg32_gfx_pixel(int x, int y, uint16_t color);
void prg32_gfx_rect(int x, int y, int w, int h, uint16_t color);
void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void prg32_gfx_clear_indexed(uint8_t index);
void prg32_gfx_pixel_indexed(int x,int y,uint8_t index);
void prg32_gfx_rect_indexed(int x,int y,int w,int h,uint8_t index);
void prg32_palette_set(uint8_t index,uint16_t color);
uint16_t prg32_palette_get(uint8_t index);
uint64_t prg32_perf_now_us(void);
int prg32_perf_begin(const prg32_perf_suite_desc_t *suite);
int prg32_perf_case_begin(const prg32_perf_case_desc_t *test_case,uint32_t samples);
int prg32_perf_record(const prg32_perf_sample_t *sample);
int prg32_perf_case_end(void);
int prg32_perf_end(void);
int prg32_perf_abort(void);
void prg32_gfx_present(void);
void prg32_audio_note(uint8_t midi_note, uint16_t duration_ms);
#endif
