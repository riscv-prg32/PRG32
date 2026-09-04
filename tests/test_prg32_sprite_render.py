from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_sprite_rendering_is_pixel_exact(tmp_path: Path) -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        pytest.skip("a host C compiler is required")
    harness = tmp_path / "sprite_render_test.c"
    harness.write_text(
        r'''
#include "prg32.h"
#include "prg32_gfx_internal.h"
#include <assert.h>
#include <string.h>

static uint16_t fb[PRG32_GAME_W * PRG32_GAME_H];
static int locks, unlocks, dirties;
void prg32_gfx_lock(void) { ++locks; }
void prg32_gfx_unlock(void) { ++unlocks; }
void prg32_gfx_pixel_unlocked(int x, int y, uint16_t color) { fb[y * PRG32_GAME_W + x] = color; }
void prg32_gfx_dirty_unlocked(int x, int y, int w, int h) {
    assert(x >= 0 && y >= 0 && x + w <= PRG32_GAME_W && y + h <= PRG32_GAME_H);
    ++dirties;
}
uint16_t *prg32_gfx_row_unlocked(int y) { return &fb[y * PRG32_GAME_W]; }

static void reset(uint16_t color) {
    for (unsigned i = 0; i < sizeof(fb) / sizeof(fb[0]); ++i) fb[i] = color;
    locks = unlocks = dirties = 0;
}

int main(void) {
    static const uint16_t palette[] = {0x0000, 0x1111, 0x2222, 0x3333,
        0x4444, 0x5555, 0x6666, 0x7777, 0x8888, 0x9999, 0xaaaa, 0xbbbb,
        0xcccc, 0xdddd, 0xeeee, 0xffff};
    static const uint8_t packed4[] = {0x12, 0x34, 0x50};
    prg32_indexed_sprite_t s4 = {packed4, palette, 5, 1, 1, 16, 4, -1};
    reset(0xbeef);
    prg32_sprite_draw_indexed(-1, 0, &s4, 0);
    assert(fb[0] == 0x2222 && fb[1] == 0x3333 && fb[2] == 0x4444 && fb[3] == 0x5555);
    assert(locks == 1 && unlocks == 1 && dirties == 1);

    static const uint8_t packed8[] = {0, 15, 1};
    prg32_indexed_sprite_t s8 = {packed8, palette, 3, 1, 1, 16, 8, 15};
    reset(0xbeef);
    prg32_sprite_draw_indexed(PRG32_GAME_W - 2, PRG32_GAME_H - 1, &s8, 0);
    assert(fb[(PRG32_GAME_H - 1) * PRG32_GAME_W + PRG32_GAME_W - 2] == 0x0000);
    assert(fb[(PRG32_GAME_H - 1) * PRG32_GAME_W + PRG32_GAME_W - 1] == 0xbeef);

    static const uint8_t packed2[] = {0x1b};
    prg32_indexed_sprite_t s2 = {packed2, palette, 4, 1, 1, 4, 2, 2};
    reset(0xbeef);
    prg32_sprite_draw_indexed(0, 0, &s2, 0);
    assert(fb[0] == 0x0000 && fb[1] == 0x1111 && fb[2] == 0xbeef && fb[3] == 0x3333);

    static const uint8_t packed1[] = {0xa0};
    prg32_indexed_sprite_t s1 = {packed1, palette, 3, 1, 1, 2, 1, -1};
    reset(0xbeef);
    prg32_sprite_draw_indexed(0, -1, &s1, 0);
    assert(locks == 0 && dirties == 0);

    static const uint16_t rgb[] = {1, 0xffff, 3, 4};
    reset(0xbeef);
    prg32_sprite_draw_frame(-1, 0, 2, 2, rgb, 0, 0xffff);
    assert(fb[0] == 0xbeef && fb[PRG32_GAME_W] == 4);
    assert(locks == 1 && unlocks == 1 && dirties == 1);
    return 0;
}
''',
        encoding="utf-8",
    )
    executable = tmp_path / "sprite_render_test"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'components/prg32/include'}",
            f"-I{ROOT / 'components/prg32'}",
            f"-I{ROOT / 'components/prg32_audio/include'}",
            str(ROOT / "components/prg32/prg32_sprite.c"),
            str(harness),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
