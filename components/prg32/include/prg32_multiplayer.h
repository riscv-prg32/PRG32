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

/* Multiplayer Server settings, chosen in the firmware's setup menu and kept in
 * NVS. These are not part of the cartridge ABI. A console uses, in order:
 *   1. a server found on the local network (mDNS, then LAN discovery), searched
 *      when Wi-Fi connects and whenever a game joins without one; on by default
 *      for classroom latency, and turned off with
 *      prg32_multiplayer_set_local_server_enabled(false);
 *   2. the address saved here (`server_url_set`);
 *   3. the compiled CONFIG_PRG32_MULTIPLAYER_SERVER_URL, if one was set;
 *   4. the public server, after 10 s of finding nothing, or at once with the
 *      local server off.
 * A local server that stops answering for 10 s gives way to 2 to 4. Each
 * returns 0 on success and -1 otherwise; `set` accepts only ws:// and wss://
 * URLs. */
int prg32_multiplayer_server_url_get(char *out_url, size_t max_len);
int prg32_multiplayer_server_url_set(const char *url);
void prg32_multiplayer_server_url_clear(void);
/* Whether a server found on the local network comes first (the default). */
bool prg32_multiplayer_local_server_enabled(void);
int prg32_multiplayer_set_local_server_enabled(bool enabled);
/* The saved address, else the compiled one. */
int prg32_multiplayer_server_url_configured(char *out_url, size_t max_len);
/* The address in use now, which may be one discovery found, else as above. */
int prg32_multiplayer_server_url_current(char *out_url, size_t max_len);
/* The public server used as a last resort, when no address is known and local
 * discovery finds nothing for 10 s (CONFIG_PRG32_MULTIPLAYER_PUBLIC_SERVER_URL). */
int prg32_multiplayer_server_url_public(char *out_url, size_t max_len);
/* True when this build can look for the _prg32mp._tcp mDNS service. */
bool prg32_multiplayer_mdns_available(void);
/* One mDNS query; writes ws://IP:PORT[/path] for the first server found. */
int prg32_multiplayer_discover_mdns(char *out_url, size_t max_len);
/* One LAN discovery search (UDP): a query, then up to `wait_ms` for an answer
 * or a beacon. Writes the server's URL and returns 0 when one answered. */
int prg32_multiplayer_discover_lan(char *out_url, size_t max_len, uint32_t wait_ms);
/* True when the address in use was found by searching the local network; the
 * setup screen shows it as LOCAL. */
bool prg32_multiplayer_server_is_local(void);
/* Uses `url`, found on the local network, as the server until the console
 * restarts or the setting changes; the next join connects there. Returns 0, or
 * -1 for an invalid URL. */
int prg32_multiplayer_use_local_server(const char *url);
/* Called by the Wi-Fi code when the console gets a network address. With the
 * local server on, it starts a short background search (mDNS, then LAN
 * discovery, for up to 10 s) so that a local server is known before a game
 * joins. Returns at once; otherwise it does nothing. */
void prg32_multiplayer_network_up(void);
/* SETUP > MULTIPLAYER SETUP, a resident modal screen. */
void prg32_setup_multiplayer_run(void);

#ifdef __cplusplus
}
#endif

#endif
