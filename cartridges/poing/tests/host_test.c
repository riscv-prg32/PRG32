#include "prg32.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void poing_init(void);
void poing_update(void);
void poing_draw(void);

static uint32_t input_state;
static uint32_t calls;
static uint32_t area;
static uint32_t hash;
static uint16_t palette[256];
static uint64_t clock_us;
static uint32_t perf_samples;

uint32_t prg32_input_read(void) { return input_state; }
void prg32_gfx_clear(uint16_t c) { hash = hash * 33u + c; ++calls; area += 64000; }
void prg32_gfx_clear_indexed(uint8_t c) { prg32_gfx_clear(c); }
void prg32_gfx_pixel(int x, int y, uint16_t c) {
    assert(x >= 0 && x < 320 && y >= 0 && y < 200);
    hash = hash * 33u + (uint32_t)x + (uint32_t)y * 320u + c; ++calls; ++area;
}
void prg32_gfx_pixel_indexed(int x,int y,uint8_t c){prg32_gfx_pixel(x,y,c);}
void prg32_gfx_rect(int x, int y, int w, int h, uint16_t c) {
    assert(w > 0 && h > 0);
    assert(x >= 0 && y >= 0 && x + w <= 320 && y + h <= 200);
    hash = hash * 33u + (uint32_t)x + (uint32_t)y * 320u + c; ++calls; area += (uint32_t)(w * h);
}
void prg32_gfx_rect_indexed(int x,int y,int w,int h,uint8_t c){prg32_gfx_rect(x,y,w,h,c);}
void prg32_gfx_text8(int x, int y, const char *t, uint16_t fg, uint16_t bg) {
    assert(x >= 0 && y >= 0 && t != 0); hash ^= fg ^ bg; ++calls;
}
void prg32_audio_note(uint8_t midi_note, uint16_t duration_ms) {
    assert(midi_note == 48 && duration_ms == 90);
}
void prg32_palette_set(uint8_t index,uint16_t color){palette[index]=color;}
uint16_t prg32_palette_get(uint8_t index){return palette[index];}
uint64_t prg32_perf_now_us(void){return ++clock_us;}
int prg32_perf_begin(const prg32_perf_suite_desc_t *suite){assert(suite&&suite->name);perf_samples=0;return 0;}
int prg32_perf_case_begin(const prg32_perf_case_desc_t *test_case,uint32_t samples){assert(test_case&&samples==300);return 0;}
int prg32_perf_record(const prg32_perf_sample_t *sample){assert(sample);++perf_samples;return 0;}
int prg32_perf_case_end(void){return perf_samples==300?0:-1;}
int prg32_perf_end(void){return 0;}
int prg32_perf_abort(void){return 0;}
void prg32_gfx_present(void){}

static uint32_t render(uint32_t input, int frames) {
    calls = area = hash = 0;
    for (int i = 0; i < frames; ++i) { input_state = input; poing_update(); poing_draw(); }
    assert(calls > 500 && area > 70000);
    return hash;
}

int main(void) {
    uint32_t baseline, moved, zoomed, frozen1, frozen2;
    poing_init();
    baseline = render(0, 1);
    moved = render(PRG32_BTN_RIGHT | PRG32_BTN_UP, 4);
    assert(moved != baseline);
    render(0, 1);
    zoomed = render(PRG32_BTN_A, 1);
    assert(zoomed != moved);
    render(0, 1);
    render(PRG32_BTN_B, 1);
    render(0, 1);
    frozen1 = render(0, 1);
    frozen2 = render(0, 1);
    assert(frozen1 == frozen2);
    render(PRG32_BTN_SELECT, 1);
    render(0, 300);
    assert(perf_samples == 300);
    puts("poing host test: PASS");
    return 0;
}
