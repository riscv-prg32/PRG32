#pragma once

#include "prg32_abi.h"

extern const prg32_abi_table_t *__prg32_abi;

typedef uint32_t (*prg32_ticks_ms_fn)(void);
static inline uint32_t prg32_ticks_ms(void) {
    return ((prg32_ticks_ms_fn)__prg32_abi->functions[PRG32_ABI_FN_PRG32_TICKS_MS])();
}

typedef uint32_t (*prg32_input_read_fn)(void);
static inline uint32_t prg32_input_read(void) {
    return ((prg32_input_read_fn)__prg32_abi->functions[PRG32_ABI_FN_PRG32_INPUT_READ])();
}
