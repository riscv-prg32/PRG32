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

typedef struct {
    int src_x;
    int src_y;
    int dst_x;
    int dst_y;
    int width;
    int height;
} prg32_sprite_clip_t;

static int clip_sprite(int x, int y, int width, int height,
                       prg32_sprite_clip_t *clip) {
    if (!clip || width <= 0 || height <= 0) return 0;
    int64_t x1 = (int64_t)x + width;
    int64_t y1 = (int64_t)y + height;
    if (x >= PRG32_GAME_W || y >= PRG32_GAME_H || x1 <= 0 || y1 <= 0) return 0;
    clip->src_x = x < 0 ? -x : 0;
    clip->src_y = y < 0 ? -y : 0;
    clip->dst_x = x < 0 ? 0 : x;
    clip->dst_y = y < 0 ? 0 : y;
    clip->width = (int)((x1 > PRG32_GAME_W ? PRG32_GAME_W : x1) - clip->dst_x);
    clip->height = (int)((y1 > PRG32_GAME_H ? PRG32_GAME_H : y1) - clip->dst_y);
    return clip->width > 0 && clip->height > 0;
}

static int blit_index8_row(uint16_t *dst, const uint8_t *src, int count,
                           const prg32_indexed_sprite_t *sprite,
                           int compare_color, uint16_t transparent_color) {
    int wrote = 0;
    for (int i = 0; i < count; ++i) {
        uint8_t index = src[i];
        if (index >= sprite->palette_count ||
            (sprite->transparent_index >= 0 && index == sprite->transparent_index)) continue;
        uint16_t color = sprite->palette[index];
        if (compare_color && color == transparent_color) continue;
        dst[i] = prg32_gfx_native_color(color);
        wrote = 1;
    }
    return wrote;
}

static int blit_index4_row(uint16_t *dst, const uint8_t *data,
                           size_t first_pixel, int count,
                           const prg32_indexed_sprite_t *sprite,
                           int compare_color, uint16_t transparent_color) {
    int wrote = 0;
    int out = 0;
#define STORE_INDEX4(value) do {                                             \
        uint8_t index_ = (value);                                            \
        if (index_ < sprite->palette_count &&                                \
            (sprite->transparent_index < 0 || index_ != sprite->transparent_index)) { \
            uint16_t color_ = sprite->palette[index_];                       \
            if (!compare_color || color_ != transparent_color) {             \
                dst[out] = prg32_gfx_native_color(color_);                   \
                wrote = 1;                                                   \
            }                                                                \
        }                                                                    \
        ++out;                                                               \
    } while (0)
    if ((first_pixel & 1u) && out < count) {
        STORE_INDEX4(data[first_pixel >> 1] & 0x0fu);
        ++first_pixel;
    }
    const uint8_t *src = data + (first_pixel >> 1);
    while (out + 1 < count) {
        uint8_t packed = *src++;
        STORE_INDEX4(packed >> 4);
        STORE_INDEX4(packed & 0x0fu);
    }
    if (out < count) STORE_INDEX4(*src >> 4);
#undef STORE_INDEX4
    return wrote;
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
    prg32_sprite_clip_t clip;
    if (!clip_sprite(x, y, 8, 8, &clip)) return;
    uint16_t native_fg = prg32_gfx_native_color(fg);
    uint16_t native_bg = prg32_gfx_native_color(bg);
    prg32_gfx_lock();
    for (int row = 0; row < clip.height; ++row) {
        uint8_t source = bits[clip.src_y + row];
        uint16_t *dst = prg32_gfx_row_unlocked(clip.dst_y + row) + clip.dst_x;
        for (int col = 0; col < clip.width; ++col) {
            int source_col = clip.src_x + col;
            dst[col] = (source & (1u << (7 - source_col))) ? native_fg : native_bg;
        }
    }
    prg32_gfx_dirty_unlocked(clip.dst_x, clip.dst_y, clip.width, clip.height);
    prg32_gfx_unlock();
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

    prg32_sprite_clip_t clip;
    if (!clip_sprite(x, y, w, h, &clip)) return;
    const uint16_t *pixels = frames + (size_t)frame * (size_t)w * (size_t)h;
    int wrote = 0;
    prg32_gfx_lock();
    for (int row = 0; row < clip.height; ++row) {
        const uint16_t *src = pixels +
            (size_t)(clip.src_y + row) * (size_t)w + (size_t)clip.src_x;
        uint16_t *dst = prg32_gfx_row_unlocked(clip.dst_y + row) + clip.dst_x;
        for (int col = 0; col < clip.width; ++col) {
            uint16_t color = src[col];
            if (color != transparent) {
                dst[col] = prg32_gfx_native_color(color);
                wrote = 1;
            }
        }
    }
    if (wrote) {
        prg32_gfx_dirty_unlocked(clip.dst_x, clip.dst_y, clip.width, clip.height);
    }
    prg32_gfx_unlock();
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

    prg32_sprite_clip_t clip;
    if (!clip_sprite(x, y, sprite->width, sprite->height, &clip)) return;

    int wrote = 0;
    prg32_gfx_lock();
    for (int visible_row = 0; visible_row < clip.height; ++visible_row) {
        int row = clip.src_y + visible_row;
        size_t row_position = (size_t)row * sprite->width;
        size_t first_position = row_position + (size_t)clip.src_x;
        uint16_t *dst = prg32_gfx_row_unlocked(clip.dst_y + visible_row) +
            clip.dst_x;
        if (!planar && sprite->bits_per_pixel == PRG32_SPRITE_BPP_8) {
            wrote |= blit_index8_row(dst, data + first_position, clip.width,
                                     sprite, compare_transparent_color,
                                     transparent_color);
            continue;
        }
        if (!planar && sprite->bits_per_pixel == PRG32_SPRITE_BPP_4) {
            wrote |= blit_index4_row(dst, data, first_position, clip.width,
                                     sprite, compare_transparent_color,
                                     transparent_color);
            continue;
        }
        size_t cached_byte_index = SIZE_MAX;
        uint8_t cached_packed_byte = 0;
        uint8_t cached_plane_bytes[8] = {0};
        for (int visible_col = 0; visible_col < clip.width; ++visible_col) {
            int col = clip.src_x + visible_col;
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
            dst[visible_col] = prg32_gfx_native_color(color);
            wrote = 1;
        }
    }
    if (wrote) {
        prg32_gfx_dirty_unlocked(clip.dst_x, clip.dst_y,
                                 clip.width, clip.height);
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
