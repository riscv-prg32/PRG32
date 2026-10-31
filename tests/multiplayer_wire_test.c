/* Compiled and run by tests/test_multiplayer_wire.py. */
#include "prg32_multiplayer_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PARSE(text) prg32_mp_parse_message((text), strlen(text), &msg)

static prg32_mp_message_t msg;

static const char *const VALID_PEER =
    "{\"type\":\"peer\",\"player_id\":4294967295,\"device_id\":\"abc\","
    "\"platform\":\"qemu\",\"x\":-32768,\"y\":32767,\"sprite\":65535,"
    "\"flags\":65535,\"input\":127,\"frame\":4294967295,\"future\":[1,{}]}";

static void test_join(void) {
    char out[PRG32_MP_JOIN_JSON_MAX];
    char long_id[PRG32_MP_LOCAL_DEVICE_ID_LEN];
    char signature[PRG32_MP_SIGNATURE_LEN];

    int n = prg32_mp_format_join(out, sizeof(out), "poing-wall:v1", 1, 2718281828u,
                                 "esp32c6", "aabbccddeeff");
    assert(n == (int)strlen(out));
    assert(strcmp(out,
                  "{\"type\":\"join\",\"signature\":\"poing-wall:v1\",\"flags\":1,"
                  "\"player_id\":2718281828,\"protocol\":\"prg32-multiplayer-1.0\","
                  "\"platform\":\"esp32c6\",\"device_id\":\"aabbccddeeff\"}") == 0);

    /* The buffer is big enough for the longest values of every field. */
    memset(long_id, 'a', sizeof(long_id) - 1);
    long_id[sizeof(long_id) - 1] = '\0';
    memset(signature, 's', sizeof(signature) - 1);
    signature[sizeof(signature) - 1] = '\0';
    assert(prg32_mp_format_join(out, sizeof(out), signature, 4294967295u, 4294967295u,
                                "prg32-qt", long_id) > 0);

    /* Truncation and invalid fields are refused, not sent. */
    assert(prg32_mp_format_join(out, 20, "room", 0, 1, "qemu", "id") == -1);
    assert(prg32_mp_format_join(out, sizeof(out), "bad room", 0, 1, "qemu", "id") == -1);
    assert(prg32_mp_format_join(out, sizeof(out), "room", 0, 0, "qemu", "id") == -1);
    assert(prg32_mp_format_join(out, sizeof(out), "room", 0, 1, "qemu", "") == -1);
    assert(prg32_mp_format_join(out, sizeof(out), "room", 0, 1, "qemu", "a\"b") == -1);
}

static void test_signature(void) {
    assert(prg32_mp_valid_signature("Az09_-.:"));
    assert(!prg32_mp_valid_signature(""));
    assert(!prg32_mp_valid_signature(NULL));
    assert(!prg32_mp_valid_signature("a b"));
    assert(prg32_mp_valid_signature("12345678901234567890123456789012345678901234567"));
    assert(!prg32_mp_valid_signature("123456789012345678901234567890123456789012345678"));
}

static void test_state(void) {
    char out[PRG32_MP_STATE_JSON_MAX];
    prg32_player_state_t state = {
        .player_id = 99, .x = -32768, .y = 32767, .sprite = 65535,
        .flags = 65535, .input = 0xffu, .frame = 4294967295u,
    };
    assert(prg32_mp_format_state(out, sizeof(out), &state) > 0);
    /* No claimed player ID, and the input mask stays within 0...127. */
    assert(strcmp(out,
                  "{\"type\":\"state\",\"x\":-32768,\"y\":32767,\"sprite\":65535,"
                  "\"flags\":65535,\"input\":127,\"frame\":4294967295}") == 0);
    assert(prg32_mp_format_state(out, 10, &state) == -1);
}

static void test_welcome(void) {
    assert(PARSE("{\"type\":\"welcome\",\"protocol\":\"prg32-multiplayer-1.0\","
                 "\"player_id\":2718281828,"
                 "\"connection_id\":\"00c799e6-d5e7-4db5-a816-a4a24912bc35\","
                 "\"device_id\":\"T_3fYx-w5Qd3P2vHaZJ9cA\",\"platform\":\"qemu\","
                 "\"persistent_device_id\":true,\"unknown\":null}") == PRG32_MP_MSG_WELCOME);
    /* Above INT32_MAX, where cJSON's valueint would saturate. */
    assert(msg.player_id == 2718281828u);
    assert(strcmp(msg.protocol, "prg32-multiplayer-1.0") == 0);
    assert(strcmp(msg.connection_id, "00c799e6-d5e7-4db5-a816-a4a24912bc35") == 0);
    assert(strcmp(msg.public_device_id, "T_3fYx-w5Qd3P2vHaZJ9cA") == 0);
    assert(strcmp(msg.platform, "qemu") == 0);
    assert(msg.persistent_device_id);

    /* The legacy shape still admits the player. */
    assert(PARSE("{\"type\":\"welcome\",\"player_id\":7}") == PRG32_MP_MSG_WELCOME);
    assert(msg.player_id == 7 && msg.public_device_id[0] == '\0');
    assert(!msg.persistent_device_id);

    /* A wrongly typed optional field is left out, not trusted or overflowed. */
    assert(PARSE("{\"type\":\"welcome\",\"player_id\":7,\"device_id\":5,"
                 "\"platform\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}") ==
           PRG32_MP_MSG_WELCOME);
    assert(msg.public_device_id[0] == '\0' && msg.platform[0] == '\0');

    assert(PARSE("{\"type\":\"welcome\"}") == PRG32_MP_MSG_INVALID);
    assert(PARSE("{\"type\":\"welcome\",\"player_id\":0}") == PRG32_MP_MSG_INVALID);
    assert(PARSE("{\"type\":\"welcome\",\"player_id\":4294967296}") == PRG32_MP_MSG_INVALID);
    assert(PARSE("{\"type\":\"welcome\",\"player_id\":\"7\"}") == PRG32_MP_MSG_INVALID);
}

static void assert_peer_rejected(const char *field, const char *value) {
    char text[256];
    int n = snprintf(text, sizeof(text),
                     "{\"type\":\"peer\",\"player_id\":1,\"x\":0,\"y\":0,\"sprite\":0,"
                     "\"flags\":0,\"input\":0,\"frame\":0,\"%s\":%s}", field, value);
    assert(n > 0 && n < (int)sizeof(text));
    /* cJSON returns the first of two duplicate names, so build it replaced. */
    char replaced[256];
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    char *first = strstr(text, pattern);
    char *end = first + strlen(pattern);
    while (*end != ',' && *end != '}') ++end;
    snprintf(replaced, sizeof(replaced), "%.*s%s%s%s", (int)(first - text), text, pattern,
             value, end);
    assert(prg32_mp_parse_message(replaced, strlen(replaced), &msg) == PRG32_MP_MSG_INVALID);
}

static void test_peer(void) {
    assert(PARSE(VALID_PEER) == PRG32_MP_MSG_PEER);
    assert(msg.peer.player_id == 4294967295u);
    assert(msg.peer.x == -32768 && msg.peer.y == 32767);
    assert(msg.peer.sprite == 65535 && msg.peer.flags == 65535);
    assert(msg.peer.input == 127);
    assert(msg.peer.frame == 4294967295u);

    assert_peer_rejected("player_id", "0");
    assert_peer_rejected("player_id", "4294967296");
    assert_peer_rejected("x", "32768");
    assert_peer_rejected("x", "-32769");
    assert_peer_rejected("x", "1.5");
    assert_peer_rejected("y", "\"3\"");
    assert_peer_rejected("y", "null");
    assert_peer_rejected("y", "true");
    assert_peer_rejected("sprite", "65536");
    assert_peer_rejected("sprite", "-1");
    assert_peer_rejected("flags", "65536");
    assert_peer_rejected("input", "128");
    assert_peer_rejected("frame", "4294967296");
    assert_peer_rejected("frame", "1e400");

    /* Missing required fields. */
    assert(PARSE("{\"type\":\"peer\",\"player_id\":1,\"x\":0}") == PRG32_MP_MSG_INVALID);
}

static void test_other_messages(void) {
    assert(PARSE("{\"type\":\"leave\",\"player_id\":2718281828}") == PRG32_MP_MSG_LEAVE);
    assert(msg.player_id == 2718281828u);
    assert(PARSE("{\"type\":\"leave\"}") == PRG32_MP_MSG_INVALID);

    assert(PARSE("{\"type\":\"pong\",\"time\":1793456789012}") == PRG32_MP_MSG_PONG);
    assert(PARSE("{\"type\":\"from-the-future\"}") == PRG32_MP_MSG_UNKNOWN);

    assert(PARSE("{\"type\":\"error\",\"code\":\"invalid_x\"}") == PRG32_MP_MSG_ERROR);
    assert(msg.error_action == PRG32_MP_ERROR_REPORT);
    assert(PARSE("{\"type\":\"error\",\"code\":\"room_full\"}") == PRG32_MP_MSG_ERROR);
    assert(msg.error_action == PRG32_MP_ERROR_ROOM_FULL);
    assert(PARSE("{\"type\":\"error\",\"code\":\"rate_limited\"}") == PRG32_MP_MSG_ERROR);
    assert(msg.error_action == PRG32_MP_ERROR_BACK_OFF);
    assert(PARSE("{\"type\":\"error\",\"code\":\"device_blocked\"}") == PRG32_MP_MSG_ERROR);
    assert(msg.error_action == PRG32_MP_ERROR_STOP);
}

static void test_malformed(void) {
    assert(PARSE("") == PRG32_MP_MSG_INVALID);
    assert(PARSE("not json") == PRG32_MP_MSG_INVALID);
    assert(PARSE("[1,2,3]") == PRG32_MP_MSG_INVALID);
    assert(PARSE("42") == PRG32_MP_MSG_INVALID);
    assert(PARSE("{\"type\":5}") == PRG32_MP_MSG_INVALID);
    assert(PARSE("{\"player_id\":5}") == PRG32_MP_MSG_INVALID);
    assert(PARSE("{\"type\":\"peer\"") == PRG32_MP_MSG_INVALID);
    assert(prg32_mp_parse_message(NULL, 0, &msg) == PRG32_MP_MSG_INVALID);

    /* The length decides, not a terminator: text after it is not read. */
    const char *text = "{\"type\":\"pong\"}GARBAGE";
    assert(prg32_mp_parse_message(text, 15, &msg) == PRG32_MP_MSG_PONG);
}

static void test_frame_order(void) {
    assert(!prg32_mp_frame_is_stale(10, 10));
    assert(!prg32_mp_frame_is_stale(11, 10));
    assert(prg32_mp_frame_is_stale(9, 10));
    /* Across the wrap, 2 comes after 4294967295. */
    assert(!prg32_mp_frame_is_stale(2, 4294967295u));
    assert(prg32_mp_frame_is_stale(4294967295u, 2));
}

int main(void) {
    test_join();
    test_signature();
    test_state();
    test_welcome();
    test_peer();
    test_other_messages();
    test_malformed();
    test_frame_order();
    puts("multiplayer wire: PASS");
    return 0;
}
