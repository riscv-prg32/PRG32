#include "prg32.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Keep the cartridge ABI entry points alive in firmware ELF output.
 * The default app does not call all of these symbols directly, so
 * --gc-sections can otherwise strip them.
 */
typedef void (*prg32_any_fn_t)(void);

static const prg32_any_fn_t g_prg32_cart_abi_exports[] = {
    (prg32_any_fn_t)prg32_ticks_ms,
    (prg32_any_fn_t)prg32_input_read,
    (prg32_any_fn_t)prg32_input_read_player,
    (prg32_any_fn_t)prg32_controller_read,
    (prg32_any_fn_t)prg32_audio_beep,
    (prg32_any_fn_t)prg32_audio_tone,
    (prg32_any_fn_t)prg32_audio_note,
    (prg32_any_fn_t)prg32_audio_play_notes,
    (prg32_any_fn_t)prg32_audio_sample_u8,
    (prg32_any_fn_t)prg32_wifi_start_mode,
    (prg32_any_fn_t)prg32_wifi_current_mode,
    (prg32_any_fn_t)prg32_wifi_setup_requested,
    (prg32_any_fn_t)prg32_wifi_setup_run,
    (prg32_any_fn_t)prg32_console_clear,
    (prg32_any_fn_t)prg32_console_putc,
    (prg32_any_fn_t)prg32_console_write,
    (prg32_any_fn_t)prg32_console_hex32,
    (prg32_any_fn_t)prg32_gfx_clear,
    (prg32_any_fn_t)prg32_gfx_present,
    (prg32_any_fn_t)prg32_gfx_pixel,
    (prg32_any_fn_t)prg32_gfx_rect,
    (prg32_any_fn_t)prg32_gfx_text8,
    (prg32_any_fn_t)prg32_debug_overlay_draw,
    (prg32_any_fn_t)prg32_keyboard_init,
    (prg32_any_fn_t)prg32_keyboard_update,
    (prg32_any_fn_t)prg32_keyboard_draw,
    (prg32_any_fn_t)prg32_text_input,
    (prg32_any_fn_t)prg32_tile_clear,
    (prg32_any_fn_t)prg32_tile_define,
    (prg32_any_fn_t)prg32_tile_put,
    (prg32_any_fn_t)prg32_tile_present,
    (prg32_any_fn_t)prg32_playfield_clear,
    (prg32_any_fn_t)prg32_playfield_put,
    (prg32_any_fn_t)prg32_playfield_get,
    (prg32_any_fn_t)prg32_playfield_scroll,
    (prg32_any_fn_t)prg32_playfield_scroll_by,
    (prg32_any_fn_t)prg32_playfield_parallax,
    (prg32_any_fn_t)prg32_playfield_camera,
    (prg32_any_fn_t)prg32_playfield_camera_x,
    (prg32_any_fn_t)prg32_playfield_camera_y,
    (prg32_any_fn_t)prg32_playfield_draw,
    (prg32_any_fn_t)prg32_playfield_draw_dual,
    (prg32_any_fn_t)prg32_playfield_present,
    (prg32_any_fn_t)prg32_platform_tile_flags,
    (prg32_any_fn_t)prg32_platform_tile_flags_get,
    (prg32_any_fn_t)prg32_platform_tile_at,
    (prg32_any_fn_t)prg32_platform_solid_at,
    (prg32_any_fn_t)prg32_platform_actor_init,
    (prg32_any_fn_t)prg32_platform_actor_move,
    (prg32_any_fn_t)prg32_platform_actor_step,
    (prg32_any_fn_t)prg32_platform_camera_follow,
    (prg32_any_fn_t)prg32_sprite_hitbox,
    (prg32_any_fn_t)prg32_sprite_draw_8x8,
    (prg32_any_fn_t)prg32_sprite_draw_16x16,
    (prg32_any_fn_t)prg32_sprite_anim_frame,
    (prg32_any_fn_t)prg32_sprite_draw_frame,
    (prg32_any_fn_t)prg32_sprite_anim_init,
    (prg32_any_fn_t)prg32_sprite_anim_update,
    (prg32_any_fn_t)prg32_sprite_anim_draw,
    (prg32_any_fn_t)prg32_score_submit,
};

void prg32_abi_exports_keep(void) {
    volatile uintptr_t sink = 0;
    size_t count =
        sizeof(g_prg32_cart_abi_exports) / sizeof(g_prg32_cart_abi_exports[0]);
    for (size_t i = 0; i < count; ++i) {
        sink ^= (uintptr_t)g_prg32_cart_abi_exports[i];
    }
    (void)sink;
}
