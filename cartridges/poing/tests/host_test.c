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

uint32_t prg32_input_read(void) { return input_state; }
void prg32_gfx_clear(uint16_t c) { hash = hash * 33u + c; ++calls; area += 64000; }
void prg32_gfx_pixel(int x, int y, uint16_t c) {
    assert(x >= 0 && x < 320 && y >= 0 && y < 200);
    hash = hash * 33u + (uint32_t)x + (uint32_t)y * 320u + c; ++calls; ++area;
}
void prg32_gfx_rect(int x, int y, int w, int h, uint16_t c) {
    assert(w > 0 && h > 0);
    assert(x >= 0 && y >= 0 && x + w <= 320 && y + h <= 200);
    hash = hash * 33u + (uint32_t)x + (uint32_t)y * 320u + c; ++calls; area += (uint32_t)(w * h);
}
void prg32_gfx_text8(int x, int y, const char *t, uint16_t fg, uint16_t bg) {
    assert(x >= 0 && y >= 0 && t != 0); hash ^= fg ^ bg; ++calls;
}
void prg32_audio_note(uint8_t midi_note, uint16_t duration_ms) {
    assert(midi_note == 48 && duration_ms == 90);
}

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
    puts("poing host test: PASS");
    return 0;
}
