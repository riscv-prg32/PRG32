#include "prg32.h"

#include <esp_random.h>

static uint32_t random_bounded(uint32_t bound) {
  if (bound == 0) return 0;

  // this function guarantees that all the possible values are uniform
  uint32_t x;

  // we limit the acceptable values to the largest that is still divisible 
  // by the bound to avoid bias 
  uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);

  do {
    x = esp_random();
  } while (x >= limit);

  return x % bound;
}

uint32_t prg32_random_number(uint32_t min, uint32_t max) {
    if (max <= min) return min;  
    return min + random_bounded(max - min + 1);
}
