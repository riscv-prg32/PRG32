#ifndef PRG32_GFX_INTERNAL_H
#define PRG32_GFX_INTERNAL_H

#include <stdint.h>

/* Internal fast path. The caller owns the graphics lock and supplies clipped
 * coordinates; one dirty rectangle is recorded after the complete draw. */
void prg32_gfx_pixel_unlocked(int x, int y, uint16_t color);
void prg32_gfx_dirty_unlocked(int x, int y, int w, int h);
uint16_t *prg32_gfx_row_unlocked(int y);

static inline uint16_t prg32_gfx_native_color(uint16_t color) {
#if defined(CONFIG_PRG32_DISPLAY_ILI9341) && CONFIG_PRG32_DISPLAY_ILI9341
    return (uint16_t)((color << 8) | (color >> 8));
#else
    return color;
#endif
}

#endif
