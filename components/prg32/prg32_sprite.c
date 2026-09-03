#include "prg32.h"
#include "prg32_gfx_internal.h"
#include <limits.h>

static void draw_compact_sprite(int x,
                                int y,
                                const prg32_indexed_sprite_t *sprite,
                                uint32_t frame,
                                int planar,
                                int compare_transparent_color,
                                uint16_t transparent_color);

static int compact_pointer_tagged(const uint16_t *frames) {
    return ((uintptr_t)frames & (uintptr_t)1u) != 0;
}

static int compact_pointer_planar(const uint16_t *frames) {
    return ((uintptr_t)frames & (uintptr_t)2u) != 0;
}

static const prg32_indexed_sprite_t *compact_pointer_asset(
    const uint16_t *frames) {
    return (const prg32_indexed_sprite_t *)
        ((uintptr_t)frames & ~(uintptr_t)3u);
}

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
    if (compact_pointer_tagged(frames)) {
        draw_compact_sprite(x, y, compact_pointer_asset(frames), frame,
                            compact_pointer_planar(frames), 1, transparent);
        return;
    }
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
    if (compact_pointer_tagged(frames)) {
        const prg32_indexed_sprite_t *asset = compact_pointer_asset(frames);
        sprite->frames = frames;
        sprite->width = asset->width;
        sprite->height = asset->height;
        sprite->frame_count = asset->frame_count;
        sprite->frame_ms = frame_ms;
        sprite->frame = 0;
        sprite->last_ms = 0;
        sprite->transparent = transparent;
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

static int compact_sprite_valid(const prg32_indexed_sprite_t *sprite,
                                uint32_t frame,
                                int planar) {
    if (!sprite || !sprite->pixels || !sprite->palette ||
        sprite->width == 0 || sprite->height == 0 ||
        frame >= sprite->frame_count || sprite->palette_count == 0 ||
        sprite->palette_count > 256) {
        return 0;
    }
    if (sprite->bits_per_pixel == 0 || sprite->bits_per_pixel > 8 ||
        sprite->palette_count > (1u << sprite->bits_per_pixel)) {
        return 0;
    }
    if (!planar && sprite->bits_per_pixel != PRG32_SPRITE_BPP_1 &&
        sprite->bits_per_pixel != PRG32_SPRITE_BPP_2 &&
        sprite->bits_per_pixel != PRG32_SPRITE_BPP_4 &&
        sprite->bits_per_pixel != PRG32_SPRITE_BPP_8) {
        return 0;
    }
    if (sprite->transparent_index < -1 ||
        sprite->transparent_index >= (int16_t)sprite->palette_count) {
        return 0;
    }
    return (size_t)sprite->width <= SIZE_MAX / (size_t)sprite->height;
}

static void draw_compact_sprite(int x,
                                int y,
                                const prg32_indexed_sprite_t *sprite,
                                uint32_t frame,
                                int planar,
                                int compare_transparent_color,
                                uint16_t transparent_color) {
    if (!compact_sprite_valid(sprite, frame, planar)) {
        return;
    }

    size_t pixel_count = (size_t)sprite->width * sprite->height;
    size_t row_bytes = ((size_t)sprite->width + 7u) / 8u;
    if (planar && row_bytes > SIZE_MAX / sprite->height) {
        return;
    }
    size_t plane_bytes = row_bytes * sprite->height;
    if ((planar && plane_bytes > SIZE_MAX / sprite->bits_per_pixel) ||
        (!planar && pixel_count > (SIZE_MAX - 7u) / sprite->bits_per_pixel)) {
        return;
    }
    size_t frame_bytes = planar
        ? plane_bytes * sprite->bits_per_pixel
        : (pixel_count * sprite->bits_per_pixel + 7u) / 8u;
    if (frame_bytes == 0 || frame > SIZE_MAX / frame_bytes) {
        return;
    }
    const uint8_t *data = sprite->pixels + (size_t)frame * frame_bytes;

    int64_t first_col_64 = x < 0 ? -(int64_t)x : 0;
    int64_t first_row_64 = y < 0 ? -(int64_t)y : 0;
    if (first_col_64 >= sprite->width || first_row_64 >= sprite->height) {
        return;
    }
    int first_col = (int)first_col_64;
    int first_row = (int)first_row_64;
    int last_col = sprite->width;
    int last_row = sprite->height;
    if ((int64_t)x + last_col > PRG32_GAME_W) last_col = PRG32_GAME_W - x;
    if ((int64_t)y + last_row > PRG32_GAME_H) last_row = PRG32_GAME_H - y;
    if (first_col >= last_col || first_row >= last_row) {
        return;
    }

    int dirty_x0 = PRG32_GAME_W;
    int dirty_y0 = PRG32_GAME_H;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    prg32_gfx_lock();
    for (int row = first_row; row < last_row; ++row) {
        size_t row_position = (size_t)row * sprite->width;
        size_t cached_byte_index = SIZE_MAX;
        uint8_t cached_packed_byte = 0;
        uint8_t cached_plane_bytes[8] = {0};
        for (int col = first_col; col < last_col; ++col) {
            size_t position = row_position + (size_t)col;
            uint8_t index = 0;
            if (planar) {
                size_t byte_index = (size_t)row * row_bytes + (size_t)col / 8u;
                uint8_t mask = (uint8_t)(0x80u >> ((unsigned)col & 7u));
                if (byte_index != cached_byte_index) {
                    for (uint8_t plane = 0;
                         plane < sprite->bits_per_pixel;
                         ++plane) {
                        cached_plane_bytes[plane] =
                            data[(size_t)plane * plane_bytes + byte_index];
                    }
                    cached_byte_index = byte_index;
                }
                for (uint8_t plane = 0; plane < sprite->bits_per_pixel; ++plane) {
                    if (cached_plane_bytes[plane] & mask) {
                        index |= (uint8_t)(1u << plane);
                    }
                }
            } else if (sprite->bits_per_pixel == PRG32_SPRITE_BPP_8) {
                index = data[position];
            } else {
                size_t bit_offset = position * sprite->bits_per_pixel;
                uint8_t shift = (uint8_t)(8u - sprite->bits_per_pixel -
                                          bit_offset % 8u);
                uint8_t mask = (uint8_t)((1u << sprite->bits_per_pixel) - 1u);
                size_t byte_index = bit_offset / 8u;
                if (byte_index != cached_byte_index) {
                    cached_packed_byte = data[byte_index];
                    cached_byte_index = byte_index;
                }
                index = (uint8_t)((cached_packed_byte >> shift) & mask);
            }
            if (index >= sprite->palette_count ||
                (sprite->transparent_index >= 0 &&
                 index == (uint16_t)sprite->transparent_index)) {
                continue;
            }
            uint16_t color = sprite->palette[index];
            if (compare_transparent_color && color == transparent_color) {
                continue;
            }
            int px = x + col;
            int py = y + row;
            prg32_gfx_pixel_unlocked(px, py, color);
            if (px < dirty_x0) dirty_x0 = px;
            if (py < dirty_y0) dirty_y0 = py;
            if (px > dirty_x1) dirty_x1 = px;
            if (py > dirty_y1) dirty_y1 = py;
        }
    }
    if (dirty_x1 >= dirty_x0) {
        prg32_gfx_dirty_unlocked(dirty_x0, dirty_y0,
                                 dirty_x1 - dirty_x0 + 1,
                                 dirty_y1 - dirty_y0 + 1);
    }
    prg32_gfx_unlock();
}

void prg32_sprite_draw_indexed(int x,
                               int y,
                               const prg32_indexed_sprite_t *sprite,
                               uint32_t frame) {
    draw_compact_sprite(x, y, sprite, frame, 0, 0, 0);
}

void prg32_sprite_draw_bitplanes(int x,
                                 int y,
                                 const prg32_indexed_sprite_t *sprite,
                                 uint32_t frame) {
    draw_compact_sprite(x, y, sprite, frame, 1, 0, 0);
}
