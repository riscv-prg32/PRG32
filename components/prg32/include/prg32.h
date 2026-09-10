#ifndef PRG32_H
#define PRG32_H

/**
 * @file prg32.h
 * @brief Stable public C interface shared by resident firmware and cartridges.
 *
 * Cartridge authors should include this header instead of ESP-IDF headers. A
 * portable cartridge calls these functions through the generated ABI table;
 * it does not link to their firmware addresses. Consequently, declaration
 * order in this file is not an ABI index: `prg32_abi.json` and the generated
 * ABI files are authoritative for exported function indices.
 *
 * Compatibility rules for this header:
 * - existing constants, structure layouts, and function signatures are kept
 *   stable for already-built cartridges;
 * - new optional services are appended to the ABI and feature-gated;
 * - pointers supplied by a cartridge must remain valid for the documented
 *   call duration (or for the asset lifetime where explicitly stated);
 * - coordinates are signed so drawing can be clipped at viewport edges;
 * - colors are native RGB565 values unless a function says otherwise.
 */

#include <stddef.h>
#include <stdint.h>
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#include "prg32_abi.h"
#include "prg32_audio.h"
#include "prg32_metrics.h"
#include "prg32_multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Physical framebuffer and cartridge-visible game viewport dimensions. The
 * 320x200 game viewport is centered vertically in the 320x240 framebuffer;
 * the remaining 20-pixel bands are owned by the resident firmware. */
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

/* Supported packed palette-index widths. Rows are densely packed, most
 * significant index first; palettes contain RGB565 entries. */
#define PRG32_SPRITE_BPP_1 1
#define PRG32_SPRITE_BPP_2 2
#define PRG32_SPRITE_BPP_4 4
#define PRG32_SPRITE_BPP_8 8

/* Per-tile collision properties used by the platform helpers. Applications
 * may combine flags; unknown bits must remain clear for forward compatibility. */
#define PRG32_TILE_FLAG_SOLID (1u << 0)
#define PRG32_TILE_FLAG_PLATFORM (1u << 1)
#define PRG32_TILE_FLAG_HAZARD (1u << 2)
#define PRG32_TILE_FLAG_COLLECT (1u << 3)

/* Result bits returned by actor movement/step calls. More than one contact can
 * be reported by one move (for example, ground plus a collectible). */
#define PRG32_PLATFORM_ON_GROUND (1u << 0)
#define PRG32_PLATFORM_HIT_LEFT (1u << 1)
#define PRG32_PLATFORM_HIT_RIGHT (1u << 2)
#define PRG32_PLATFORM_HIT_HEAD (1u << 3)
#define PRG32_PLATFORM_HAZARD (1u << 4)
#define PRG32_PLATFORM_COLLECT (1u << 5)

/* Stable byte offsets for assembly cartridges. They mirror
 * prg32_platform_actor_t exactly and must not be changed independently. */
#define PRG32_PLATFORM_ACTOR_X_OFFSET 0
#define PRG32_PLATFORM_ACTOR_Y_OFFSET 4
#define PRG32_PLATFORM_ACTOR_VX_OFFSET 8
#define PRG32_PLATFORM_ACTOR_VY_OFFSET 12
#define PRG32_PLATFORM_ACTOR_W_OFFSET 16
#define PRG32_PLATFORM_ACTOR_H_OFFSET 18
#define PRG32_PLATFORM_ACTOR_STATE_OFFSET 20
#define PRG32_PLATFORM_ACTOR_LAYER_OFFSET 22
#define PRG32_PLATFORM_ACTOR_SIZE 24

/* Controller bitmask. Player 1 occupies bits 0..6 and player 2 occupies
 * bits 8..14. SELECT and START are names for the same physical input. */
#define PRG32_BTN_LEFT (1u << 0)
#define PRG32_BTN_RIGHT (1u << 1)
#define PRG32_BTN_UP (1u << 2)
#define PRG32_BTN_DOWN (1u << 3)
#define PRG32_BTN_A (1u << 4)
#define PRG32_BTN_B (1u << 5)
#define PRG32_BTN_START (1u << 6)
#define PRG32_BTN_SELECT PRG32_BTN_START

#define PRG32_P2_BTN_LEFT (1u << 8)
#define PRG32_P2_BTN_RIGHT (1u << 9)
#define PRG32_P2_BTN_UP (1u << 10)
#define PRG32_P2_BTN_DOWN (1u << 11)
#define PRG32_P2_BTN_A (1u << 12)
#define PRG32_P2_BTN_B (1u << 13)
#define PRG32_P2_BTN_START (1u << 14)
#define PRG32_P2_BTN_SELECT PRG32_P2_BTN_START

/* Common RGB565 colors. Custom colors use RRRRRGGGGGGBBBBB packing. */
#define PRG32_COLOR_BLACK 0x0000
#define PRG32_COLOR_WHITE 0xffff
#define PRG32_COLOR_RED 0xf800
#define PRG32_COLOR_GREEN 0x07e0
#define PRG32_COLOR_BLUE 0x001f
#define PRG32_COLOR_YELLOW 0xffe0
#define PRG32_COLOR_CYAN 0x07ff
#define PRG32_COLOR_MAGENTA 0xf81f

/* Firmware-owned status-band identifiers. */
#define PRG32_BAND_TOP 0
#define PRG32_BAND_BOTTOM 1

/* Cartridge binary-format constants. These values are consumed by packaging
 * tools and the runtime loader; changing one invalidates stored binaries. */
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
#ifndef CONFIG_PRG32_CART_MAX_KIB
#define CONFIG_PRG32_CART_MAX_KIB 128
#endif
#define PRG32_CART_MAX_SIZE ((uint32_t)CONFIG_PRG32_CART_MAX_KIB * 1024u)
#ifndef CONFIG_PRG32_CART_RAM_KIB
#define CONFIG_PRG32_CART_RAM_KIB 32
#endif
#define PRG32_CART_RAM_SIZE ((uint32_t)CONFIG_PRG32_CART_RAM_KIB * 1024u)
#define PRG32_CART_NAME_LEN 32
#define PRG32_CART_SLOT_COUNT 4
#ifndef PRG32_FIRMWARE_VERSION
#define PRG32_FIRMWARE_VERSION "dev"
#endif

typedef struct __attribute__((packed)) {
  /* Legacy fixed header. Multi-byte fields are little-endian on PRG32. */
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
  /* Extended portable header. The initial fields intentionally match
   * prg32_cart_header_t so old inspection tools can read the common prefix. */
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
  /* Snapshot of one cartridge slot. Strings are always NUL-terminated by the
   * runtime. `loaded` and `stored` are boolean bytes. */
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
  /* Animation descriptor for contiguous RGB565 frames. `frames` must remain
   * valid while the descriptor is used; frame_ms==0 selects frame zero. */
  const uint16_t *frames;
  uint16_t width;
  uint16_t height;
  uint16_t frame_count;
  uint16_t frame_ms;
  uint32_t frame;
  uint32_t last_ms;
  uint16_t transparent;
} prg32_anim_sprite_t;

/* Compact sprite assets keep palette indices in cartridge memory and are
 * expanded directly into the native RGB565 framebuffer while drawing. */
typedef struct {
  /* Platform actor uses integer game-pixel coordinates and velocities. State
   * contains PRG32_PLATFORM_* result bits from the most recent move. */
  const uint8_t *pixels;
  const uint16_t *palette;
  uint16_t width;
  uint16_t height;
  uint16_t frame_count;
  uint16_t palette_count;
  uint8_t bits_per_pixel;
  int16_t transparent_index;
} prg32_indexed_sprite_t;

/* Compact descriptors can also travel through the existing RGB565 sprite ABI.
 * uint16_t assets are naturally aligned, so bit zero remains available as a
 * format tag without changing any function prototype or animation structure. */
#define PRG32_SPRITE_INDEXED(asset)                                           \
  ((const uint16_t *)((uintptr_t)(asset) | (uintptr_t)1u))
#define PRG32_SPRITE_BITPLANES(asset)                                         \
  ((const uint16_t *)((uintptr_t)(asset) | (uintptr_t)3u))

typedef struct {
  /* Wi-Fi credentials copied by prg32_wifi_start_mode(); callers may release
   * this structure after that call returns. */
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
  /* Stateful on-screen keyboard. The caller owns `buffer`; capacity includes
   * the terminating NUL byte and must remain valid until editing finishes. */
  prg32_wifi_mode_t mode;
  char ssid[32];
  char password[64];
  char ap_ssid[32];
  char ap_password[64];
} prg32_wifi_config_t;

typedef struct {
  /* One blocking legacy note: frequency in Hz followed by duration in ms. */
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

/** @name Runtime and input
 * `prg32_init()` is firmware-owned; cartridges normally implement their own
 * init/update/draw callbacks and must not call it. Time values use the
 * wrapping 32-bit millisecond clock, so compare elapsed differences rather
 * than absolute deadlines. Input readers return PRG32_BTN_* masks; edge
 * detection remains the caller's responsibility.
 * @{ */
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
/** @} */

/** @name Basic audio and RGB indicator
 * Basic audio calls are safe when an output is unavailable: they become
 * no-ops. Frequencies are Hz, durations are ms, MIDI notes use 0..127, and
 * sample_u8 consumes unsigned 8-bit PCM centered on 128 for the duration of
 * the call. The resident runtime owns audio initialization and master volume;
 * portable cartridges do not initialize hardware directly.
 * @{ */
void prg32_audio_beep(uint32_t hz, uint32_t ms);
/*
 * Low-level PWM tone generation.
 * - 'hz': frequency in Hertz.
 * - 'ms': duration in milliseconds.
 * - 'duty': duty cycle (PWM ON/OFF percentage), used to control the volume
 *           of the buzzer by limiting electrical power.
 */
void prg32_audio_tone(uint32_t hz, uint32_t ms, uint16_t duty);
/* Play a MIDI-pitched note through the compatibility voice. The call observes
 * the user's persisted master-volume limit and does not require cartridge-side
 * audio initialization. */
void prg32_audio_note(uint8_t midi_note, uint32_t ms);
void prg32_audio_play_notes(const prg32_note_t *notes, size_t count);
void prg32_audio_sample_u8(const uint8_t *samples, size_t count,
                           uint32_t sample_rate);
int prg32_rgb_led_init(int gpio);
int prg32_rgb_led_available(void);
void prg32_rgb_led_set(uint8_t red, uint8_t green, uint8_t blue);
void prg32_rgb_led_off(void);
void prg32_rgb_led_vu(uint8_t level);
void prg32_audio_led_vu_enable(int enabled);
int prg32_audio_led_vu_enabled(void);
void prg32_audio_led_vu_level(uint8_t level);
/** @} */

typedef struct {
  /* Score record copied out of resident storage. */
  char game[24];
  char player[24];
  uint32_t score;
} prg32_score_t;

/** @name Network setup and score service
 * Functions returning int use 0 for success unless their name returns a count
 * or boolean. Output buffers always include their capacity; pass a nonzero
 * size. Network operations can fail or be compiled as harmless stubs, so a
 * cartridge must retain a complete offline path.
 * @{ */
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
int prg32_score_reset_local(const char *game);
int prg32_score_count(const char *game);
int prg32_score_get(const char *game, int index, prg32_score_t *out_score);
int prg32_scoreboard_show(const char *game, const char *title);
int prg32_score_submit_remote(const char *base_url, const char *game,
                              const char *player, uint32_t score);
/** @} */

/** @name Cartridge Store integration
 * URL getters copy normalized NUL-terminated URLs into caller storage.
 * Discover and ping may block on network timeouts; invoke them from menus, not
 * a per-frame draw callback. Setup functions run resident modal UI.
 * @{ */
int prg32_store_url_get(char *out_url, size_t max_len);
int prg32_store_url_set(const char *url);
void prg32_store_url_clear(void);
int prg32_store_url_resolve(char *out_url, size_t max_len);
int prg32_store_discover(char *out_url, size_t max_len);
int prg32_store_ping(const char *base_url, char *out_name, size_t name_len);
void prg32_setup_store_run(void);
void prg32_setup_store_browse_run(void);
/** @} */

/** @name Cartridge loader and slots
 * Slot numbers are 0..PRG32_CART_SLOT_COUNT-1. Install/load functions validate
 * magic, ABI, feature bits, sizes, CRC, and portable imports before publishing
 * a new generation. Stream writes must be ordered, non-overlapping chunks and
 * concluded with the exact total size. Error-returning calls leave diagnostic
 * text available through prg32_cart_last_error().
 * @{ */
void prg32_cart_init(void);
uintptr_t prg32_cart_load_addr(void);
size_t prg32_cart_ram_size(void);
uint32_t prg32_cart_generation(void);
int prg32_cart_is_loaded(void);
int prg32_cart_load_stored(void);
int prg32_cart_install(const void *image, size_t image_size, int persist);
int prg32_cart_install_slot(uint8_t slot, const void *image, size_t image_size,
                            int persist);
int prg32_cart_store_slot(uint8_t slot, const void *image, size_t image_size);
int prg32_cart_erase_slot(uint8_t slot);
size_t prg32_cart_slot_size(uint8_t slot);
int prg32_cart_stream_begin(uint8_t slot, size_t image_size);
int prg32_cart_stream_write(uint8_t slot, size_t offset, const void *data,
                            size_t len);
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
/** @} */

/** @name Text console
 * Console output targets the 40x25 character surface. Strings are consumed
 * synchronously and are not retained after the call.
 * @{ */
void prg32_console_clear(void);
void prg32_console_putc(int ch);
void prg32_console_write(const char *s);
void prg32_console_hex32(uint32_t value);
/** @} */

/** @name Immediate-mode graphics and status bands
 * Cartridge coordinates address the 320x200 viewport unless fullscreen mode
 * is enabled by resident UI. Drawing is clipped; non-positive rectangle sizes
 * draw nothing. `present` publishes the completed back buffer. Cartridge draw
 * callbacks are already serialized by the runtime and normally should not
 * acquire the graphics lock themselves.
 *
 * Snapshot rows copy RGB565 pixels into caller storage and return the number
 * copied or a negative error. Band text is copied by the runtime. Fullscreen,
 * persistent band configuration, and lock management are primarily resident
 * firmware services and should be used cautiously by cartridges.
 * @{ */
void prg32_gfx_clear(uint16_t color);
void prg32_gfx_clear_indexed(uint8_t index);
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
void prg32_gfx_pixel_indexed(int x, int y, uint8_t index);
void prg32_gfx_rect_indexed(int x, int y, int w, int h, uint8_t index);
void prg32_palette_set(uint8_t index, uint16_t rgb565);
uint16_t prg32_palette_get(uint8_t index);
void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg);
int prg32_gfx_snapshot_row_rgb565(int y, uint16_t *out, size_t pixels);
void prg32_splash_draw(const char *title, const char *subtitle, uint16_t bg,
                       uint16_t fg, uint16_t accent);
void prg32_splash_show(const char *title, const char *subtitle,
                       uint32_t duration_ms, uint16_t bg, uint16_t fg,
                       uint16_t accent);
void prg32_splash_draw_game(const char *title, const char *subtitle,
                            uint16_t bg, uint16_t fg, uint16_t accent);
void prg32_splash_show_game(const char *title, const char *subtitle,
                            uint32_t duration_ms, uint16_t bg, uint16_t fg,
                            uint16_t accent);
void prg32_splash_show_default(void);
void prg32_debug_overlay_draw(int enabled, int x, int y, uint32_t input_mask,
                              uint32_t frame);
/** @} */

/** @name On-screen text entry
 * Initialize once with caller-owned storage, then update once per frame and
 * draw as needed. `update` returns nonzero when editing completes; inspect the
 * descriptor's done/cancelled fields to distinguish the outcome.
 * @{ */
void prg32_keyboard_init(prg32_keyboard_t *keyboard, char *buffer,
                         size_t capacity);
int prg32_keyboard_update(prg32_keyboard_t *keyboard, uint32_t input_mask);
void prg32_keyboard_draw(const prg32_keyboard_t *keyboard, int x, int y);
int prg32_text_input(char *buffer, size_t capacity, const char *title);
/** @} */

/** @name Tiles and scrolling playfields
 * Tiles are 8x8 one-bit masks expanded with RGB565 foreground/background
 * colors. Playfield maps contain tile IDs; layer, tile, and map coordinates
 * outside their documented ranges are ignored (getters return zero).
 *
 * Scroll and camera positions use pixels. Parallax factors are Q8 fixed point,
 * where PRG32_PARALLAX_1X means one screen pixel per camera pixel.
 * `transparent_zero` treats tile ID zero as transparent. The present helper
 * draws the configured playfield composition and publishes it.
 * @{ */
void prg32_tile_clear(uint16_t color);
void prg32_tile_define(uint8_t id, const uint8_t *bitmap8x8, uint16_t fg,
                       uint16_t bg);
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
/** @} */

/** @name Platform collision helpers
 * Collision uses axis-aligned integer pixel bounds against tile flags.
 * actor_move applies a requested delta with collision resolution and returns
 * PRG32_PLATFORM_* contacts. actor_step additionally derives horizontal and
 * jump movement from an input mask and applies gravity, all in integer units
 * per update. Call actor_init before either operation.
 * @{ */
void prg32_platform_tile_flags(uint8_t tile_id, uint8_t flags);
uint8_t prg32_platform_tile_flags_get(uint8_t tile_id);
uint8_t prg32_platform_tile_at(uint8_t layer, int pixel_x, int pixel_y);
int prg32_platform_solid_at(uint8_t layer, int pixel_x, int pixel_y);
void prg32_platform_actor_init(prg32_platform_actor_t *actor, uint8_t layer,
                               int x, int y, int w, int h);
uint16_t prg32_platform_actor_move(prg32_platform_actor_t *actor, int dx,
                                   int dy);
uint16_t prg32_platform_actor_step(prg32_platform_actor_t *actor,
                                   uint32_t input_mask, int move_speed,
                                   int jump_speed, int gravity, int max_fall);
void prg32_platform_camera_follow(const prg32_platform_actor_t *actor,
                                  int deadzone_x, int deadzone_y);
/** @} */

/** @name Sprites and animation
 * Hitboxes use half-open rectangles and return a boolean intersection.
 * RGB565 sprite functions consume row-major pixels. A pixel equal to the
 * transparent key is skipped. Indexed frame data remains packed in cartridge
 * memory and is expanded at draw time; frame indices wrap by frame_count.
 *
 * PRG32_SPRITE_INDEXED/PRG32_SPRITE_BITPLANES are tagged-pointer adapters for
 * legacy RGB565 sprite entry points. Use only naturally aligned static asset
 * descriptors, never arbitrary or dynamically byte-aligned pointers.
 * @{ */
int prg32_sprite_hitbox(int ax, int ay, int aw, int ah, int bx, int by, int bw,
                        int bh);
void prg32_sprite_draw_8x8(int x, int y, const uint8_t *bits, uint16_t fg,
                           uint16_t bg);
void prg32_sprite_draw_16x16(int x, int y, const uint16_t *rgb565);
void prg32_sprite_draw_24x24(int x, int y, const uint16_t *rgb565);
uint32_t prg32_sprite_anim_frame(uint32_t now_ms, uint32_t frame_count,
                                 uint32_t frame_ms);
void prg32_sprite_draw_frame(int x, int y, int w, int h, const uint16_t *frames,
                             uint32_t frame, uint16_t transparent);
void prg32_sprite_anim_init(prg32_anim_sprite_t *sprite, const uint16_t *frames,
                            uint16_t width, uint16_t height,
                            uint16_t frame_count, uint16_t frame_ms,
                            uint16_t transparent);
void prg32_sprite_anim_update(prg32_anim_sprite_t *sprite, uint32_t now_ms);
void prg32_sprite_anim_draw(const prg32_anim_sprite_t *sprite, int x, int y);
void prg32_sprite_draw_indexed(int x, int y,
                               const prg32_indexed_sprite_t *sprite,
                               uint32_t frame);
void prg32_sprite_draw_bitplanes(int x, int y,
                                 const prg32_indexed_sprite_t *sprite,
                                 uint32_t frame);
/** @} */

/* Assembly demos export per-game init/update/draw symbols selected by main. */

/**
 * @brief Memory statistics for the PRG32 runtime.
 *
 * NOTE: The ESP-IDF API /api/memory also supports retrieving per-task dynamic
 * heap allocations and boot memory checkpoints if you build with tracking enabled:
 * `python3 -m prg32 esp32c6 build --enable-heap-tracking`.
 * That configuration alters the heap allocator struct size and therefore cannot
 * be enabled solely from prg32 code. If enabled, the Python CLI tool will
 * automatically present a detailed dynamic memory breakdown.
 * However this option introduces overhead and should only be enabled during the
 * information collection.
 */
typedef struct {
  /* Values are an instantaneous best-effort snapshot and can change as other
   * FreeRTOS tasks allocate memory. Sizes are bytes. */
  uint32_t static_bss_bytes;
  uint32_t static_data_bytes;
  uint32_t heap_total_bytes;
  uint32_t heap_free_bytes;
  uint32_t heap_allocated_bytes;
  uint32_t heap_largest_free_block;
} prg32_memory_stats_t;

/** Fill caller-owned statistics and optionally emit the same summary to the
 * firmware log. Passing NULL to prg32_memory_get_stats is a no-op. */
void prg32_memory_get_stats(prg32_memory_stats_t *stats);
void prg32_memory_log_stats(void);

#ifdef __cplusplus
}
#endif
#endif
