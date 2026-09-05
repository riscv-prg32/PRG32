#ifndef PRG32_H
#define PRG32_H
#include <stdint.h>
#include <stdbool.h>
#define PRG32_BTN_LEFT (1u<<0)
#define PRG32_BTN_RIGHT (1u<<1)
#define PRG32_BTN_UP (1u<<2)
#define PRG32_BTN_DOWN (1u<<3)
#define PRG32_BTN_A (1u<<4)
#define PRG32_BTN_B (1u<<5)
#define PRG32_BTN_SELECT (1u<<6)
#define PRG32_MP_FLAG_ENABLE (1u<<0)
typedef struct { const uint8_t *pixels; const uint16_t *palette; uint16_t width,height; uint16_t frame_count; uint16_t palette_count; uint8_t bits_per_pixel; int16_t transparent_index; } prg32_indexed_sprite_t;
typedef struct { uint32_t player_id; int16_t x,y; uint16_t sprite,flags; uint32_t input,frame,last_seen_ms; } prg32_player_state_t;
uint32_t prg32_ticks_ms(void); uint32_t prg32_input_read(void);
void prg32_band_set_game_info(const char *s);
void prg32_audio_play_track(int t); void prg32_audio_note_on_pan(int ch,int inst,int note,int vel,int pan); void prg32_audio_note_off(int ch);
void prg32_score_submit_current_player(const char*,uint32_t); void prg32_scoreboard_show(const char*,const char*);
void prg32_multiplayer_init(void); bool prg32_multiplayer_available(void); int prg32_multiplayer_join(const char*,uint32_t); int prg32_multiplayer_leave(void); void prg32_multiplayer_tick(void); int prg32_multiplayer_set_input(uint32_t); int prg32_multiplayer_set_local_state(int16_t,int16_t,uint16_t,uint16_t); int prg32_multiplayer_get_peer_count(void); int prg32_multiplayer_get_peer(int,prg32_player_state_t*);
void prg32_gfx_clear(uint16_t); void prg32_gfx_rect(int,int,int,int,uint16_t); void prg32_gfx_text8(int,int,const char*,uint16_t,uint16_t); void prg32_sprite_draw_8x8(int,int,const uint8_t*,uint16_t,uint16_t); void prg32_sprite_draw_indexed(int,int,const prg32_indexed_sprite_t*,uint32_t);
#endif
