#ifndef PRG32_H
#define PRG32_H

#include <stdint.h>
#include <stddef.h>
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#include "prg32_audio.h"
#include "prg32_metrics.h"
#include "prg32_multiplayer.h"
#include "prg32_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRG32_LCD_W 320
#define PRG32_LCD_H 240
#define PRG32_GAME_W 320
#define PRG32_GAME_H 200
#define PRG32_SCOREBOARD_TOP_MAX 5
#define PRG32_TEXT_COLS 40
#define PRG32_TEXT_ROWS 25
#define PRG32_TILE_W 8
#define PRG32_TILE_H 8
#define PRG32_TILE_COLS 40
#define PRG32_TILE_ROWS 25
#define PRG32_PLAYFIELD_LAYERS 2
#define PRG32_PLAYFIELD_COLS 64
#define PRG32_PLAYFIELD_ROWS 32
#define PRG32_PARALLAX_1X 256

#define PRG32_TILE_FLAG_SOLID    (1u << 0)
#define PRG32_TILE_FLAG_PLATFORM (1u << 1)
#define PRG32_TILE_FLAG_HAZARD   (1u << 2)
#define PRG32_TILE_FLAG_COLLECT  (1u << 3)

#define PRG32_PLATFORM_ON_GROUND (1u << 0)
#define PRG32_PLATFORM_HIT_LEFT  (1u << 1)
#define PRG32_PLATFORM_HIT_RIGHT (1u << 2)
#define PRG32_PLATFORM_HIT_HEAD  (1u << 3)
#define PRG32_PLATFORM_HAZARD    (1u << 4)
#define PRG32_PLATFORM_COLLECT   (1u << 5)

#define PRG32_PLATFORM_ACTOR_X_OFFSET     0
#define PRG32_PLATFORM_ACTOR_Y_OFFSET     4
#define PRG32_PLATFORM_ACTOR_VX_OFFSET    8
#define PRG32_PLATFORM_ACTOR_VY_OFFSET    12
#define PRG32_PLATFORM_ACTOR_W_OFFSET     16
#define PRG32_PLATFORM_ACTOR_H_OFFSET     18
#define PRG32_PLATFORM_ACTOR_STATE_OFFSET 20
#define PRG32_PLATFORM_ACTOR_LAYER_OFFSET 22
#define PRG32_PLATFORM_ACTOR_SIZE         24

#define PRG32_BTN_LEFT  (1u << 0)
#define PRG32_BTN_RIGHT (1u << 1)
#define PRG32_BTN_UP    (1u << 2)
#define PRG32_BTN_DOWN  (1u << 3)
#define PRG32_BTN_A     (1u << 4)
#define PRG32_BTN_B     (1u << 5)
#define PRG32_BTN_START (1u << 6)
#define PRG32_BTN_SELECT PRG32_BTN_START

#define PRG32_P2_BTN_LEFT  (1u << 8)
#define PRG32_P2_BTN_RIGHT (1u << 9)
#define PRG32_P2_BTN_UP    (1u << 10)
#define PRG32_P2_BTN_DOWN  (1u << 11)
#define PRG32_P2_BTN_A     (1u << 12)
#define PRG32_P2_BTN_B     (1u << 13)
#define PRG32_P2_BTN_START (1u << 14)
#define PRG32_P2_BTN_SELECT PRG32_P2_BTN_START

#define PRG32_COLOR_BLACK   0x0000
#define PRG32_COLOR_WHITE   0xffff
#define PRG32_COLOR_RED     0xf800
#define PRG32_COLOR_GREEN   0x07e0
#define PRG32_COLOR_BLUE    0x001f
#define PRG32_COLOR_YELLOW  0xffe0
#define PRG32_COLOR_CYAN    0x07ff
#define PRG32_COLOR_MAGENTA 0xf81f

#define PRG32_BAND_TOP 0
#define PRG32_BAND_BOTTOM 1

#define PRG32_CART_MAGIC "PRG2"
#define PRG32_CART_ABI_MAJOR 1
#define PRG32_CART_ABI_MINOR 1
#define PRG32_CART_FLAG_AUDIO_BLOCK (1u << 0)
#define PRG32_CART_FLAG_MULTIPLAYER (1u << 1)
#define PRG32_CART_FLAG_ABI_TABLE (1u << 2)
#define PRG32_CART_FLAG_RELOCATABLE (1u << 3)
#define PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE 0u
#define PRG32_IMPORT_MODEL_ABI_TABLE 1u
#define PRG32_CART_META_MAGIC "PRG32META"
#define PRG32_CART_META_VERSION 1
#define PRG32_CART_META_ABI "prg32-metadata-1.0"
#define PRG32_CART_COLOPHON_ABI "prg32-colophon-1.0"
#define PRG32_CART_META_BLOCK_META "META"
#define PRG32_CART_META_BLOCK_ICON "ICON"
#define PRG32_CART_META_BLOCK_SCREENSHOT "SCRN"
#define PRG32_CART_META_BLOCK_SIGNATURE "SIGN"
#define PRG32_CART_META_BLOCK_COLOPHON "COLO"
#define PRG32_CART_ARCH_ESP32C6 "esp32c6"
#define PRG32_CART_ARCH_QEMU "qemu"
#define PRG32_CART_LOAD_ADDR 0x40800000u
#define PRG32_CART_MAX_SIZE (64u * 1024u)
#ifndef CONFIG_PRG32_CART_RAM_KIB
#define CONFIG_PRG32_CART_RAM_KIB 32
#endif
#define PRG32_CART_RAM_SIZE ((uint32_t)CONFIG_PRG32_CART_RAM_KIB * 1024u)
#define PRG32_CART_NAME_LEN 32
#define PRG32_CART_SLOT_COUNT 2
#ifndef PRG32_FIRMWARE_VERSION
#define PRG32_FIRMWARE_VERSION "dev"
#endif

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t header_size;
    uint16_t flags;
    uint32_t load_addr;
    uint32_t code_size;
    uint32_t mem_size;
    uint32_t init_offset;
    uint32_t update_offset;
    uint32_t draw_offset;
    uint32_t payload_crc32;
    char name[PRG32_CART_NAME_LEN];
} prg32_cart_header_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t header_size;
    uint16_t flags;
    uint32_t load_addr;
    uint32_t code_size;
    uint32_t mem_size;
    uint32_t init_offset;
    uint32_t update_offset;
    uint32_t draw_offset;
    uint32_t payload_crc32;
    char name[PRG32_CART_NAME_LEN];
    uint32_t abi_hash;
    uint32_t required_features;
    uint32_t optional_features;
    uint32_t isa_flags;
    uint32_t relocation_offset;
    uint32_t relocation_count;
    uint32_t import_model;
} prg32_cart_header_v2_t;

typedef struct {
    char slot_name[8];
    char name[PRG32_CART_NAME_LEN];
    uint32_t load_addr;
    uint32_t code_size;
    uint32_t mem_size;
    uint32_t audio_size;
    uint32_t generation;
    uint16_t flags;
    uint8_t slot;
    uint8_t loaded;
    uint8_t stored;
    uint8_t audio;
} prg32_cart_info_t;

typedef struct {
    const uint16_t *frames;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t frame_ms;
    uint32_t frame;
    uint32_t last_ms;
    uint16_t transparent;
} prg32_anim_sprite_t;

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    uint16_t w;
    uint16_t h;
    uint16_t state;
    uint8_t layer;
    uint8_t reserved;
} prg32_platform_actor_t;

typedef enum {
    PRG32_WIFI_MODE_OFF = 0,
    PRG32_WIFI_MODE_STA = 1,
    PRG32_WIFI_MODE_AP = 2,
    PRG32_WIFI_MODE_APSTA = 3,
} prg32_wifi_mode_t;

typedef enum {
    PRG32_BAND_MODE_NONE = 0,
    PRG32_BAND_MODE_FPS = 1,
    PRG32_BAND_MODE_WIFI = 2,
    PRG32_BAND_MODE_GAME = 3,
    PRG32_BAND_MODE_DEBUG = 4,
    PRG32_BAND_MODE_CUSTOM = 5,
} prg32_band_mode_t;

typedef struct {
    prg32_wifi_mode_t mode;
    char ssid[32];
    char password[64];
    char ap_ssid[32];
    char ap_password[64];
} prg32_wifi_config_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    uint8_t cursor;
    uint8_t page;
    uint8_t shift;
    uint8_t done;
    uint8_t cancelled;
    uint32_t last_input;
} prg32_keyboard_t;

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
} prg32_note_t;

void prg32_init(void);
void prg32_set_mode(uint32_t mode);
uint32_t prg32_ticks_ms(void);
uint32_t prg32_input_read(void);
uint32_t prg32_input_read_player(uint8_t player);
uint32_t prg32_input_read_menu(void);
void prg32_input_wait_released(uint32_t mask);
uint32_t prg32_controller_read(void);
const char *prg32_controller_name(uint32_t bit);
void prg32_diag_set_input_state(uint32_t input_state);
void prg32_diag_increment_frame(void);
uint32_t prg32_diag_input_state(void);
uint32_t prg32_diag_frame_count(void);
void prg32_audio_beep(uint32_t hz, uint32_t ms);
void prg32_audio_tone(uint32_t hz, uint32_t ms, uint16_t duty);
void prg32_audio_note(uint8_t midi_note, uint32_t ms);
void prg32_audio_play_notes(const prg32_note_t *notes, size_t count);
void prg32_audio_sample_u8(const uint8_t *samples,
                           size_t count,
                           uint32_t sample_rate);
int prg32_rgb_led_init(int gpio);
int prg32_rgb_led_available(void);
void prg32_rgb_led_set(uint8_t red, uint8_t green, uint8_t blue);
void prg32_rgb_led_off(void);
void prg32_rgb_led_vu(uint8_t level);
void prg32_audio_led_vu_enable(int enabled);
int prg32_audio_led_vu_enabled(void);
void prg32_audio_led_vu_level(uint8_t level);

typedef struct {
    char game[24];
    char player[24];
    uint32_t score;
} prg32_score_t;
void prg32_wifi_scores_init(void);
int prg32_wifi_start_mode(const prg32_wifi_config_t *config);
prg32_wifi_mode_t prg32_wifi_current_mode(void);
const char *prg32_wifi_current_ip(void);
const char *prg32_wifi_current_ssid(void);
int prg32_wifi_setup_requested(void);
int prg32_wifi_setup_run(void);
void prg32_scores_api_start(void);
int prg32_score_player_get(char *out_player, size_t max_len);
int prg32_score_player_set(const char *player);
int prg32_score_player_prompt(void);
int prg32_score_submit(const char *game, const char *player, uint32_t score);
int prg32_score_submit_current_player(const char *game, uint32_t score);
int prg32_score_sync_remote(void);
int prg32_score_count(const char *game);
int prg32_score_get(const char *game, int index, prg32_score_t *out_score);
int prg32_scoreboard_show(const char *game, const char *title);
int prg32_score_submit_remote(const char *base_url,
                              const char *game,
                              const char *player,
                              uint32_t score);

/* CartridgeStore integration. */
int prg32_store_url_get(char *out_url, size_t max_len);
int prg32_store_url_set(const char *url);
void prg32_store_url_clear(void);
int prg32_store_url_resolve(char *out_url, size_t max_len);
int prg32_store_discover(char *out_url, size_t max_len);
int prg32_store_ping(const char *base_url, char *out_name, size_t name_len);
void prg32_setup_store_run(void);
void prg32_setup_store_browse_run(void);

void prg32_cart_init(void);
uintptr_t prg32_cart_load_addr(void);
size_t prg32_cart_ram_size(void);
uint32_t prg32_cart_generation(void);
int prg32_cart_is_loaded(void);
int prg32_cart_load_stored(void);
int prg32_cart_install(const void *image, size_t image_size, int persist);
int prg32_cart_install_slot(uint8_t slot,
                            const void *image,
                            size_t image_size,
                            int persist);
int prg32_cart_store_slot(uint8_t slot, const void *image, size_t image_size);
size_t prg32_cart_slot_size(uint8_t slot);
int prg32_cart_stream_begin(uint8_t slot, size_t image_size);
int prg32_cart_stream_write(uint8_t slot, size_t offset, const void *data, size_t len);
int prg32_cart_stream_end(uint8_t slot, size_t image_size);
int prg32_cart_select_stored(void);
int prg32_cart_select_slot(uint8_t slot);
int prg32_cart_default_slot(void);
int prg32_cart_set_default_slot(int slot);
int prg32_cart_select_default(void);
int prg32_cart_stored_count(void);
int prg32_cart_get_slot_info(uint8_t slot, prg32_cart_info_t *info);
int prg32_cart_get_info(prg32_cart_info_t *info);
int prg32_cart_call_init(void);
int prg32_cart_call_update(void);
int prg32_cart_call_draw(void);
const char *prg32_cart_last_error(void);

void prg32_console_clear(void);
void prg32_console_putc(int ch);
void prg32_console_write(const char *s);
void prg32_console_hex32(uint32_t value);

void prg32_gfx_clear(uint16_t color);
void prg32_gfx_present(void);
void prg32_gfx_lock(void);
int prg32_gfx_try_lock(uint32_t timeout_ms);
void prg32_gfx_unlock(void);
void prg32_gfx_set_fullscreen(int enabled);
int prg32_gfx_fullscreen_enabled(void);
void prg32_gfx_set_band_color(uint16_t color);
void prg32_gfx_use_background_bands(void);
void prg32_band_set_mode(uint8_t band, prg32_band_mode_t mode);
prg32_band_mode_t prg32_band_mode(uint8_t band);
const char *prg32_band_mode_name(prg32_band_mode_t mode);
void prg32_band_set_text(uint8_t band, const char *text);
void prg32_band_set_game_info(const char *text);
void prg32_band_log(const char *message);
void prg32_band_set_colors(uint8_t band, uint16_t fg, uint16_t bg);
void prg32_band_use_default_colors(uint8_t band);
void prg32_band_load_config(void);
void prg32_band_save_config(void);
void prg32_gfx_pixel(int x, int y, uint16_t color);
void prg32_gfx_rect(int x, int y, int w, int h, uint16_t color);
void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg);
int prg32_gfx_snapshot_row_rgb565(int y, uint16_t *out, size_t pixels);
void prg32_splash_draw(const char *title,
                       const char *subtitle,
                       uint16_t bg,
                       uint16_t fg,
                       uint16_t accent);
void prg32_splash_show(const char *title,
                       const char *subtitle,
                       uint32_t duration_ms,
                       uint16_t bg,
                       uint16_t fg,
                       uint16_t accent);
void prg32_splash_draw_game(const char *title,
                            const char *subtitle,
                            uint16_t bg,
                            uint16_t fg,
                            uint16_t accent);
void prg32_splash_show_game(const char *title,
                            const char *subtitle,
                            uint32_t duration_ms,
                            uint16_t bg,
                            uint16_t fg,
                            uint16_t accent);
void prg32_splash_show_default(void);
void prg32_debug_overlay_draw(int enabled,
                              int x,
                              int y,
                              uint32_t input_mask,
                              uint32_t frame);

void prg32_keyboard_init(prg32_keyboard_t *keyboard,
                         char *buffer,
                         size_t capacity);
int prg32_keyboard_update(prg32_keyboard_t *keyboard, uint32_t input_mask);
void prg32_keyboard_draw(const prg32_keyboard_t *keyboard, int x, int y);
int prg32_text_input(char *buffer,
                     size_t capacity,
                     const char *title);

void prg32_tile_clear(uint16_t color);
void prg32_tile_define(uint8_t id, const uint8_t *bitmap8x8, uint16_t fg, uint16_t bg);
void prg32_tile_put(uint8_t tx, uint8_t ty, uint8_t id);
void prg32_tile_present(void);
void prg32_playfield_clear(uint8_t layer, uint8_t tile_id);
void prg32_playfield_put(uint8_t layer, uint8_t tx, uint8_t ty, uint8_t id);
uint8_t prg32_playfield_get(uint8_t layer, uint8_t tx, uint8_t ty);
void prg32_playfield_scroll(uint8_t layer, int x, int y);
void prg32_playfield_scroll_by(uint8_t layer, int dx, int dy);
void prg32_playfield_parallax(uint8_t layer, int x_q8, int y_q8);
void prg32_playfield_camera(int x, int y);
int prg32_playfield_camera_x(void);
int prg32_playfield_camera_y(void);
void prg32_playfield_draw(uint8_t layer, int transparent_zero);
void prg32_playfield_draw_dual(void);
void prg32_playfield_present(void);

void prg32_platform_tile_flags(uint8_t tile_id, uint8_t flags);
uint8_t prg32_platform_tile_flags_get(uint8_t tile_id);
uint8_t prg32_platform_tile_at(uint8_t layer, int pixel_x, int pixel_y);
int prg32_platform_solid_at(uint8_t layer, int pixel_x, int pixel_y);
void prg32_platform_actor_init(prg32_platform_actor_t *actor,
                               uint8_t layer,
                               int x,
                               int y,
                               int w,
                               int h);
uint16_t prg32_platform_actor_move(prg32_platform_actor_t *actor,
                                   int dx,
                                   int dy);
uint16_t prg32_platform_actor_step(prg32_platform_actor_t *actor,
                                   uint32_t input_mask,
                                   int move_speed,
                                   int jump_speed,
                                   int gravity,
                                   int max_fall);
void prg32_platform_camera_follow(const prg32_platform_actor_t *actor,
                                  int deadzone_x,
                                  int deadzone_y);

int prg32_sprite_hitbox(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);
void prg32_sprite_draw_8x8(int x, int y, const uint8_t *bits, uint16_t fg, uint16_t bg);
void prg32_sprite_draw_16x16(int x, int y, const uint16_t *rgb565);
void prg32_sprite_draw_24x24(int x, int y, const uint16_t *rgb565);
uint32_t prg32_sprite_anim_frame(uint32_t now_ms,
                                 uint32_t frame_count,
                                 uint32_t frame_ms);
void prg32_sprite_draw_frame(int x,
                             int y,
                             int w,
                             int h,
                             const uint16_t *frames,
                             uint32_t frame,
                             uint16_t transparent);
void prg32_sprite_anim_init(prg32_anim_sprite_t *sprite,
                            const uint16_t *frames,
                            uint16_t width,
                            uint16_t height,
                            uint16_t frame_count,
                            uint16_t frame_ms,
                            uint16_t transparent);
void prg32_sprite_anim_update(prg32_anim_sprite_t *sprite, uint32_t now_ms);
void prg32_sprite_anim_draw(const prg32_anim_sprite_t *sprite, int x, int y);

/* Assembly demos export per-game init/update/draw symbols selected by main. */

#ifdef __cplusplus
}
#endif
#endif
