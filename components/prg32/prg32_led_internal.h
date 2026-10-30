#ifndef PRG32_LED_INTERNAL_H
#define PRG32_LED_INTERNAL_H

/*
 * The breadboard1 PCB shares its addressable RGB data pin with LCD D/C.
 * These helpers are firmware-internal; cartridges use the public LED API,
 * which rejects pins owned by another peripheral.
 */
void prg32_shared_rgb_led_boot_flash(int gpio);
void prg32_shared_rgb_led_off(int gpio);

#endif
