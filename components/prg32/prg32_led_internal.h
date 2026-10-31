#ifndef PRG32_LED_INTERNAL_H
#define PRG32_LED_INTERNAL_H

/*
 * On the breadboard1 and pcb1 profiles the DevKitC-1 addressable RGB data
 * pin (GPIO8) is also LCD D/C (PRG32_PIN_SHARED_RGB_LED).
 * These helpers are firmware-internal; cartridges use the public LED API,
 * which rejects pins owned by another peripheral.
 */
void prg32_shared_rgb_led_boot_flash(int gpio);
void prg32_shared_rgb_led_off(int gpio);

#endif
