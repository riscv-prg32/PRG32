#ifndef PRG32_MULTIPLAYER_H
#define PRG32_MULTIPLAYER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

void prg32_multiplayer_init(void);
bool prg32_multiplayer_available(void);
int prg32_multiplayer_join(const char *cartridge_signature, uint32_t flags);
int prg32_multiplayer_leave(void);
void prg32_multiplayer_tick(void);
int prg32_multiplayer_set_local_state(int16_t x,
                                      int16_t y,
                                      uint16_t sprite,
                                      uint16_t flags);
int prg32_multiplayer_set_input(uint32_t input);
int prg32_multiplayer_get_peer_count(void);
int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out);

#ifdef __cplusplus
}
#endif

#endif
