#include "prg32.h"

int prg32_sprite_hitbox(int ax,
                        int ay,
                        int aw,
                        int ah,
                        int bx,
                        int by,
                        int bw,
                        int bh) {
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) {
        return 0;
    }
    if (ax + aw <= bx) {
        return 0;
    }
    if (bx + bw <= ax) {
        return 0;
    }
    if (ay + ah <= by) {
        return 0;
    }
    if (by + bh <= ay) {
        return 0;
    }
    return 1;
}

void prg32_sprite_draw_8x8(int x,
                           int y,
                           const uint8_t *bits,
                           uint16_t fg,
                           uint16_t bg) {
    if (!bits) {
        return;
    }
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            uint16_t color = (bits[row] & (1u << (7 - col))) ? fg : bg;
            prg32_gfx_pixel(x + col, y + row, color);
        }
    }
}

void prg32_sprite_draw_16x16(int x, int y, const uint16_t *rgb565) {
    if (!rgb565) {
        return;
    }
    prg32_sprite_draw_frame(x, y, 16, 16, rgb565, 0, PRG32_COLOR_WHITE);
}

void prg32_sprite_draw_24x24(int x, int y, const uint16_t *rgb565) {
    if (!rgb565) {
        return;
    }
    prg32_sprite_draw_frame(x, y, 24, 24, rgb565, 0, PRG32_COLOR_WHITE);
}

uint32_t prg32_sprite_anim_frame(uint32_t now_ms,
                                 uint32_t frame_count,
                                 uint32_t frame_ms) {
    if (frame_count == 0 || frame_ms == 0) {
        return 0;
    }
    return (now_ms / frame_ms) % frame_count;
}

void prg32_sprite_draw_frame(int x,
                             int y,
                             int w,
                             int h,
                             const uint16_t *frames,
                             uint32_t frame,
                             uint16_t transparent) {
    if (!frames || w <= 0 || h <= 0) {
        return;
    }

    const uint16_t *pixels = frames + (size_t)frame * (size_t)w * (size_t)h;
    for (int row = 0; row < h; ++row) {
        int py = y + row;
        if ((unsigned)py >= PRG32_GAME_H) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            int px = x + col;
            if ((unsigned)px >= PRG32_GAME_W) {
                continue;
            }
            uint16_t color = pixels[row * w + col];
            if (color != transparent) {
                prg32_gfx_pixel(px, py, color);
            }
        }
    }
}

void prg32_sprite_anim_init(prg32_anim_sprite_t *sprite,
                            const uint16_t *frames,
                            uint16_t width,
                            uint16_t height,
                            uint16_t frame_count,
                            uint16_t frame_ms,
                            uint16_t transparent) {
    if (!sprite) {
        return;
    }
    sprite->frames = frames;
    sprite->width = width;
    sprite->height = height;
    sprite->frame_count = frame_count;
    sprite->frame_ms = frame_ms;
    sprite->frame = 0;
    sprite->last_ms = 0;
    sprite->transparent = transparent;
}

void prg32_sprite_anim_update(prg32_anim_sprite_t *sprite, uint32_t now_ms) {
    if (!sprite || sprite->frame_count == 0 || sprite->frame_ms == 0) {
        return;
    }
    if (sprite->last_ms == 0) {
        sprite->last_ms = now_ms;
        return;
    }

    uint32_t elapsed = now_ms - sprite->last_ms;
    uint32_t steps = elapsed / sprite->frame_ms;
    if (steps == 0) {
        return;
    }
    sprite->frame = (sprite->frame + steps) % sprite->frame_count;
    sprite->last_ms += steps * sprite->frame_ms;
}

void prg32_sprite_anim_draw(const prg32_anim_sprite_t *sprite, int x, int y) {
    if (!sprite) {
        return;
    }
    prg32_sprite_draw_frame(x,
                            y,
                            sprite->width,
                            sprite->height,
                            sprite->frames,
                            sprite->frame,
                            sprite->transparent);
}
