/* The small part of the PRG32 API the Poing Wall uses, for the host test. Keep
 * these declarations in step with components/prg32/include/prg32.h. */
#ifndef PRG32_H
#define PRG32_H

#include <stdbool.h>
#include <stdint.h>

#define PRG32_BTN_A (1u << 4)
#define PRG32_MP_MAX_PEERS 8
#define PRG32_MP_FLAG_ENABLE (1u << 0)

typedef struct {
    uint32_t player_id;
    int16_t x;
    int16_t y;
    uint16_t sprite;
    uint16_t flags;
    uint32_t input;
    uint32_t frame;
    uint32_t last_seen_ms;
} prg32_player_state_t;

uint32_t prg32_ticks_ms(void);
uint32_t prg32_input_read(void);
void prg32_gfx_clear_indexed(uint8_t index);
void prg32_gfx_pixel_indexed(int x, int y, uint8_t index);
void prg32_gfx_rect_indexed(int x, int y, int w, int h, uint8_t index);
void prg32_gfx_text8(int x, int y, const char *text, uint16_t fg, uint16_t bg);
void prg32_palette_set(uint8_t index, uint16_t rgb565);
uint16_t prg32_palette_get(uint8_t index);
void prg32_audio_note(uint8_t channel, uint8_t wave, uint8_t note, uint8_t velocity, uint16_t duration_ms);

void prg32_multiplayer_init(void);
bool prg32_multiplayer_available(void);
int prg32_multiplayer_join(const char *cartridge_signature, uint32_t flags);
void prg32_multiplayer_tick(void);
uint32_t prg32_multiplayer_local_player_id(void);
int prg32_multiplayer_set_local_state(int16_t x, int16_t y, uint16_t sprite, uint16_t flags);
int prg32_multiplayer_set_input(uint32_t input);
int prg32_multiplayer_get_peer_count(void);
int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out);

#endif
