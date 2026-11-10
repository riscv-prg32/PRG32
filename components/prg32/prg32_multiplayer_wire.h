#ifndef PRG32_MULTIPLAYER_WIRE_H
#define PRG32_MULTIPLAYER_WIRE_H

/* JSON messages of the prg32-multiplayer-1.0 protocol, without any networking,
 * so that tests/test_multiplayer_wire.py can compile and check them on a
 * computer. The transport lives in prg32_multiplayer.c. The server repository's
 * docs/protocol.md is the normative reference. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prg32_multiplayer.h"

#define PRG32_MP_PROTOCOL "prg32-multiplayer-1.0"
#define PRG32_MP_PROTOCOL_LEN 32

/* 47 characters plus the terminator (protocol: "Signature maximum"). */
#define PRG32_MP_SIGNATURE_LEN 48
/* 128 characters plus the terminator (protocol: "Device source maximum"). */
#define PRG32_MP_LOCAL_DEVICE_ID_LEN 129
/* 22 base64url characters plus the terminator. */
#define PRG32_MP_PUBLIC_DEVICE_ID_LEN 23
/* A UUID string plus the terminator. */
#define PRG32_MP_CONNECTION_ID_LEN 37
/* The longest registered value is "prg32-qt". */
#define PRG32_MP_PLATFORM_LEN 16
#define PRG32_MP_ERROR_CODE_LEN 32

/* Largest texts the formatters can produce, derived from the field limits:
 *   join:  about 90 characters of names and punctuation, plus 47 (signature),
 *          10 (flags), 10 (player_id), 21 (protocol), 15 (platform), and 128
 *          (device_id).
 *   state: about 60 characters of names and punctuation, plus 6, 6, 5, 5, 3,
 *          and 10 for the numbers. */
#define PRG32_MP_JOIN_JSON_MAX 336
#define PRG32_MP_STATE_JSON_MAX 128

#define PRG32_MP_LEAVE_JSON "{\"type\":\"leave\"}"

/* LAN discovery (the server's docs/protocol.md, "LAN discovery"): a console
 * broadcasts this UDP datagram to PRG32_MULTIPLAYER_DISCOVERY_PORT, and a
 * server answers, or beacons, {"type":"server","protocol":...,"url":"ws://..."}. */
#define PRG32_MP_DISCOVER_JSON \
    "{\"type\":\"discover\",\"protocol\":\"" PRG32_MP_PROTOCOL "\"}"
/* A server URL, ws:// or wss://, plus the terminator. */
#define PRG32_MP_URL_LEN 128

typedef enum {
    PRG32_MP_MSG_INVALID = 0, /* Malformed, or a field failed validation. */
    PRG32_MP_MSG_UNKNOWN,     /* A valid object of a type this runtime ignores. */
    PRG32_MP_MSG_WELCOME,
    PRG32_MP_MSG_PEER,
    PRG32_MP_MSG_LEAVE,
    PRG32_MP_MSG_PONG,
    PRG32_MP_MSG_ERROR,
} prg32_mp_msg_type_t;

/* What a client should do about a server `error` message. */
typedef enum {
    PRG32_MP_ERROR_REPORT = 0, /* The message was rejected; the session goes on. */
    PRG32_MP_ERROR_ROOM_FULL,  /* Not admitted: try the join again later. */
    PRG32_MP_ERROR_BACK_OFF,   /* Policy: the server closes; reconnect slowly. */
    PRG32_MP_ERROR_STOP,       /* Blocked: do not reconnect by itself. */
} prg32_mp_error_action_t;

typedef struct {
    prg32_mp_msg_type_t type;
    /* welcome and leave: the player; peer: inside `peer`. */
    uint32_t player_id;
    prg32_player_state_t peer;
    /* welcome, all optional: a legacy server sends only type and player_id.
     * Each is an empty string when the server left it out. */
    char protocol[PRG32_MP_PROTOCOL_LEN];
    char connection_id[PRG32_MP_CONNECTION_ID_LEN];
    char public_device_id[PRG32_MP_PUBLIC_DEVICE_ID_LEN];
    char platform[PRG32_MP_PLATFORM_LEN];
    bool persistent_device_id;
    /* error */
    char error_code[PRG32_MP_ERROR_CODE_LEN];
    prg32_mp_error_action_t error_action;
} prg32_mp_message_t;

/* mDNS discovery: a PRG32 Multiplayer Server on the local network advertises
 * this DNS-SD service. Its SRV record gives the port; the optional TXT keys
 * `scheme` (ws or wss, default ws) and `path` (default /) complete the URL. */
#define PRG32_MP_MDNS_SERVICE "_prg32mp"
#define PRG32_MP_MDNS_PROTO "_tcp"

/* True when `url` starts with ws:// or wss://, has a host, contains only
 * printable characters without spaces, and fits PRG32_MP_URL_LEN. */
int prg32_mp_valid_server_url(const char *url);

/* Builds "scheme://host:port/path" from an mDNS answer. A NULL or empty scheme
 * means ws; a NULL, empty or "/" path adds nothing. Returns the URL length, or
 * -1 when an argument is invalid or the URL does not fit `out`. */
int prg32_mp_format_server_url(char *out,
                               size_t out_size,
                               const char *scheme,
                               const char *host,
                               uint16_t port,
                               const char *path);

/* Reads a discovery answer or beacon. Copies its URL into `url` and returns 0
 * when the datagram is a `server` message of this protocol whose URL starts
 * with ws:// or wss://, has a host, and fits; returns -1 otherwise. */
int prg32_mp_parse_discovery(const char *data, size_t len, char *url, size_t url_size);

/* Checks the signature grammar: 1 to 47 characters of A-Z a-z 0-9 _ - . : */
int prg32_mp_valid_signature(const char *signature);

/* Each formatter returns the text length, or -1 when an argument is invalid or
 * the text does not fit `out`. */
int prg32_mp_format_join(char *out,
                         size_t out_size,
                         const char *signature,
                         uint32_t flags,
                         uint32_t player_id,
                         const char *platform,
                         const char *device_id);
int prg32_mp_format_state(char *out,
                          size_t out_size,
                          const prg32_player_state_t *state);

/* Parses one received text message of `len` bytes, which need not end in NUL.
 * Every number is checked for type, integral value, and range before it is
 * narrowed. Returns the message type, which is also stored in `out->type`. */
prg32_mp_msg_type_t prg32_mp_parse_message(const char *data,
                                           size_t len,
                                           prg32_mp_message_t *out);

/* True when `frame` is older than `latest`, with unsigned 32-bit wraparound. */
bool prg32_mp_frame_is_stale(uint32_t frame, uint32_t latest);

#endif
