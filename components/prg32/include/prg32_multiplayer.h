#ifndef PRG32_MULTIPLAYER_H
#define PRG32_MULTIPLAYER_H

#include <stdbool.h>
#include <stddef.h>
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
/* The player ID the server confirmed for this session, or 0 until then. It can
 * change after a reconnection. Do not use it as a persistent device identity. */
uint32_t prg32_multiplayer_local_player_id(void);
int prg32_multiplayer_set_local_state(int16_t x,
                                      int16_t y,
                                      uint16_t sprite,
                                      uint16_t flags);
int prg32_multiplayer_set_input(uint32_t input);
int prg32_multiplayer_get_peer_count(void);
int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out);

/* Multiplayer Server address, used by the firmware's setup menu. These are not
 * part of the cartridge ABI. No address is built in: a console uses the one
 * saved here, else the compiled CONFIG_PRG32_MULTIPLAYER_SERVER_URL if one was
 * set, else the first server that mDNS or LAN discovery finds. Each returns 0
 * on success and -1 otherwise; `set` accepts only ws:// and wss:// URLs. */
int prg32_multiplayer_server_url_get(char *out_url, size_t max_len);
int prg32_multiplayer_server_url_set(const char *url);
void prg32_multiplayer_server_url_clear(void);
/* The saved address, else the compiled one. */
int prg32_multiplayer_server_url_configured(char *out_url, size_t max_len);
/* The address in use now, which may be one discovery found, else as above. */
int prg32_multiplayer_server_url_current(char *out_url, size_t max_len);
/* True when this build can look for the _prg32mp._tcp mDNS service. */
bool prg32_multiplayer_mdns_available(void);
/* One mDNS query; writes ws://IP:PORT[/path] for the first server found. */
int prg32_multiplayer_discover_mdns(char *out_url, size_t max_len);
/* SETUP > MULTIPLAYER SETUP, a resident modal screen. */
void prg32_setup_multiplayer_run(void);

#ifdef __cplusplus
}
#endif

#endif
