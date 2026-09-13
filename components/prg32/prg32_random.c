#include "prg32.h"

#include "esp_random.h"

uint32_t prg32_random_number(uint32_t min, uint32_t max) {
    if (max <= min) {
        return min;
    }

    /* Unsigned wrap gives zero only when the requested span is 2^32. */
    uint32_t span = max - min + 1u;
    if (span == 0u) {
        return esp_random();
    }

    /* Reject the incomplete leading interval to avoid modulo bias. */
    uint32_t threshold = (uint32_t)(-span) % span;
    uint32_t sample;
    do {
        sample = esp_random();
    } while (sample < threshold);

    return min + sample % span;
}
