#ifndef PRG32_GFX_INTERNAL_H
#define PRG32_GFX_INTERNAL_H

#include <stdint.h>

/* Internal fast path. The caller owns the graphics lock and supplies clipped
 * coordinates; one dirty rectangle is recorded after the complete draw. */
void prg32_gfx_pixel_unlocked(int x, int y, uint16_t color);
void prg32_gfx_dirty_unlocked(int x, int y, int w, int h);

#endif
