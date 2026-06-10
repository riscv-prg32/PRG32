#pragma once

#include <stdint.h>
#include "prg32_abi_hash.h"
#include "prg32_abi_index.h"

#define PRG32_ABI_MAGIC 0x49424150u

#define PRG32_FEATURE_AUDIO        (1u << 0)
#define PRG32_FEATURE_WIFI         (1u << 1)
#define PRG32_FEATURE_MULTIPLAYER  (1u << 2)
#define PRG32_FEATURE_METRICS      (1u << 3)
#define PRG32_FEATURE_AUDIO_PLUS   (1u << 4)
#define PRG32_FEATURE_KEYBOARD     (1u << 5)
#define PRG32_FEATURE_TILEMAP      (1u << 6)
#define PRG32_FEATURE_PLATFORMER   (1u << 7)
#define PRG32_FEATURE_SPRITES      (1u << 8)

typedef struct prg32_abi_table {
    uint32_t magic;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t struct_size;
    uint16_t fn_count;
    uint32_t abi_hash;
    uint32_t provided_features;
    const void *functions[PRG32_ABI_FN_COUNT];
} prg32_abi_table_t;

extern const prg32_abi_table_t prg32_abi_table;
