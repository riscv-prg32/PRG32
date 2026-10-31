#include "prg32_multiplayer_wire.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

int prg32_mp_valid_signature(const char *signature) {
    if (!signature || signature[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; signature[i] != '\0'; ++i) {
        char ch = signature[i];
        if (i >= PRG32_MP_SIGNATURE_LEN - 1) {
            return 0;
        }
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.' || ch == ':')) {
            return 0;
        }
    }
    return 1;
}

/* The platform and the device ID are written into JSON without escaping, so
 * only characters that never need it are accepted. MAC addresses and UUIDs
 * pass. */
static int valid_identity_text(const char *text, size_t max_len) {
    if (!text || text[0] == '\0') {
        return 0;
    }
    size_t len = strlen(text);
    if (len > max_len) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        char ch = text[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '-')) {
            return 0;
        }
    }
    return 1;
}

int prg32_mp_format_join(char *out,
                         size_t out_size,
                         const char *signature,
                         uint32_t flags,
                         uint32_t player_id,
                         const char *platform,
                         const char *device_id) {
    if (!out || !prg32_mp_valid_signature(signature) || player_id == 0 ||
        !valid_identity_text(platform, PRG32_MP_PLATFORM_LEN - 1) ||
        !valid_identity_text(device_id, PRG32_MP_LOCAL_DEVICE_ID_LEN - 1)) {
        return -1;
    }
    int n = snprintf(out,
                     out_size,
                     "{\"type\":\"join\",\"signature\":\"%s\","
                     "\"flags\":%" PRIu32 ",\"player_id\":%" PRIu32 ","
                     "\"protocol\":\"" PRG32_MP_PROTOCOL "\","
                     "\"platform\":\"%s\",\"device_id\":\"%s\"}",
                     signature,
                     flags,
                     player_id,
                     platform,
                     device_id);
    return (n > 0 && (size_t)n < out_size) ? n : -1;
}

int prg32_mp_format_state(char *out,
                          size_t out_size,
                          const prg32_player_state_t *state) {
    if (!out || !state) {
        return -1;
    }
    /* No player_id: the server takes the sender from the connection. */
    int n = snprintf(out,
                     out_size,
                     "{\"type\":\"state\",\"x\":%d,\"y\":%d,\"sprite\":%u,"
                     "\"flags\":%u,\"input\":%" PRIu32 ",\"frame\":%" PRIu32 "}",
                     (int)state->x,
                     (int)state->y,
                     (unsigned)state->sprite,
                     (unsigned)state->flags,
                     state->input & 0x7fu,
                     state->frame);
    return (n > 0 && (size_t)n < out_size) ? n : -1;
}

/* Reads an exact integer in [min, max]. Numeric strings, null, booleans,
 * fractions, infinities, and values out of range all fail, as the protocol
 * requires. cJSON's valueint saturates at INT_MAX, so it cannot hold a player
 * ID or a frame counter: the check uses the double, which represents every
 * 32-bit integer exactly. */
static int json_integer(const cJSON *root,
                        const char *name,
                        double min,
                        double max,
                        double *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item)) {
        return -1;
    }
    double value = item->valuedouble;
    /* The negated form also rejects NaN. */
    if (!(value >= min && value <= max)) {
        return -1;
    }
    if ((double)(int64_t)value != value) {
        return -1;
    }
    *out = value;
    return 0;
}

static int json_player_id(const cJSON *root, uint32_t *out) {
    double value = 0;
    if (json_integer(root, "player_id", 1.0, 4294967295.0, &value) != 0) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

/* Copies an optional string member, leaving an empty string when it is absent,
 * has another type, or does not fit. */
static void json_copy_string(const cJSON *root,
                             const char *name,
                             char *out,
                             size_t out_size) {
    out[0] = '\0';
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsString(item) && item->valuestring &&
        strlen(item->valuestring) < out_size) {
        strcpy(out, item->valuestring);
    }
}

static int parse_peer(const cJSON *root, prg32_player_state_t *peer) {
    double x, y, sprite, flags, input, frame;
    if (json_player_id(root, &peer->player_id) != 0 ||
        json_integer(root, "x", -32768.0, 32767.0, &x) != 0 ||
        json_integer(root, "y", -32768.0, 32767.0, &y) != 0 ||
        json_integer(root, "sprite", 0.0, 65535.0, &sprite) != 0 ||
        json_integer(root, "flags", 0.0, 65535.0, &flags) != 0 ||
        json_integer(root, "input", 0.0, 127.0, &input) != 0 ||
        json_integer(root, "frame", 0.0, 4294967295.0, &frame) != 0) {
        return -1;
    }
    peer->x = (int16_t)x;
    peer->y = (int16_t)y;
    peer->sprite = (uint16_t)sprite;
    peer->flags = (uint16_t)flags;
    peer->input = (uint32_t)input;
    peer->frame = (uint32_t)frame;
    return 0;
}

static prg32_mp_error_action_t error_action(const char *code) {
    if (strcmp(code, "room_full") == 0) {
        return PRG32_MP_ERROR_ROOM_FULL;
    }
    if (strcmp(code, "rate_limited") == 0) {
        return PRG32_MP_ERROR_BACK_OFF;
    }
    if (strcmp(code, "device_blocked") == 0) {
        return PRG32_MP_ERROR_STOP;
    }
    return PRG32_MP_ERROR_REPORT;
}

prg32_mp_msg_type_t prg32_mp_parse_message(const char *data,
                                           size_t len,
                                           prg32_mp_message_t *out) {
    if (!out) {
        return PRG32_MP_MSG_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->type = PRG32_MP_MSG_INVALID;
    if (!data || len == 0) {
        return out->type;
    }

    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return out->type;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        cJSON_Delete(root);
        return out->type;
    }

    if (strcmp(type->valuestring, "welcome") == 0) {
        if (json_player_id(root, &out->player_id) == 0) {
            const cJSON *persistent =
                cJSON_GetObjectItemCaseSensitive(root, "persistent_device_id");
            json_copy_string(root, "protocol", out->protocol,
                             sizeof(out->protocol));
            json_copy_string(root, "connection_id", out->connection_id,
                             sizeof(out->connection_id));
            json_copy_string(root, "device_id", out->public_device_id,
                             sizeof(out->public_device_id));
            json_copy_string(root, "platform", out->platform,
                             sizeof(out->platform));
            out->persistent_device_id = cJSON_IsTrue(persistent);
            out->type = PRG32_MP_MSG_WELCOME;
        }
    } else if (strcmp(type->valuestring, "peer") == 0) {
        if (parse_peer(root, &out->peer) == 0) {
            out->player_id = out->peer.player_id;
            out->type = PRG32_MP_MSG_PEER;
        }
    } else if (strcmp(type->valuestring, "leave") == 0) {
        if (json_player_id(root, &out->player_id) == 0) {
            out->type = PRG32_MP_MSG_LEAVE;
        }
    } else if (strcmp(type->valuestring, "pong") == 0) {
        out->type = PRG32_MP_MSG_PONG;
    } else if (strcmp(type->valuestring, "error") == 0) {
        json_copy_string(root, "code", out->error_code, sizeof(out->error_code));
        out->error_action = error_action(out->error_code);
        out->type = PRG32_MP_MSG_ERROR;
    } else {
        out->type = PRG32_MP_MSG_UNKNOWN;
    }

    cJSON_Delete(root);
    return out->type;
}

bool prg32_mp_frame_is_stale(uint32_t frame, uint32_t latest) {
    return (int32_t)(frame - latest) < 0;
}
