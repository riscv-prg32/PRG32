#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "esp_system.h"

#ifndef PRG32_RESTART_HOTKEY_ENABLE
#define PRG32_RESTART_HOTKEY_ENABLE 1
#endif

#ifndef PRG32_PIN_BTN_SELECT
#define PRG32_PIN_BTN_SELECT PRG32_PIN_BTN_START
#endif

#define PRG32_RESTART_HOTKEY_P1 \
    (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_DOWN)

uint32_t prg32_qemu_input_read(void);

/*
 * PRG32 controller layer.
 *
 * ESP32-C6 has a USB Serial/JTAG device controller, but it is not a normal
 * USB host port for plugging in arbitrary USB HID gamepads. Therefore this
 * file keeps input deliberately small:
 *   1. GPIO buttons on the reference breadboard/PCB.
 *   2. A QEMU host-terminal keyboard mapper for desktop tests.
 *
 * The game sees only the stable PRG32 bitmask: LEFT/RIGHT/UP/DOWN/SELECT/A/B.
 * This is intentionally similar to memory-mapped input registers on 1980s
 * consoles, which makes it a useful Computer Architecture teaching example.
 */

static uint32_t read_gpio_buttons(void) {
    uint32_t v = 0;
    if (PRG32_PIN_BTN_LEFT >= 0 && !gpio_get_level(PRG32_PIN_BTN_LEFT)) {
        v |= PRG32_BTN_LEFT;
    }
    if (PRG32_PIN_BTN_RIGHT >= 0 && !gpio_get_level(PRG32_PIN_BTN_RIGHT)) {
        v |= PRG32_BTN_RIGHT;
    }
    if (PRG32_PIN_BTN_UP >= 0 && !gpio_get_level(PRG32_PIN_BTN_UP)) {
        v |= PRG32_BTN_UP;
    }
    if (PRG32_PIN_BTN_DOWN >= 0 && !gpio_get_level(PRG32_PIN_BTN_DOWN)) {
        v |= PRG32_BTN_DOWN;
    }
    if (PRG32_PIN_BTN_A >= 0 && !gpio_get_level(PRG32_PIN_BTN_A)) {
        v |= PRG32_BTN_A;
    }
    if (PRG32_PIN_BTN_B >= 0 && !gpio_get_level(PRG32_PIN_BTN_B)) {
        v |= PRG32_BTN_B;
    }
    if (PRG32_PIN_BTN_START >= 0 && !gpio_get_level(PRG32_PIN_BTN_START)) {
        v |= PRG32_BTN_SELECT;
    }
    if (PRG32_PIN_BTN_SELECT >= 0 && !gpio_get_level(PRG32_PIN_BTN_SELECT)) {
        v |= PRG32_BTN_SELECT;
    }
    return v;
}

uint32_t prg32_controller_read(void) {
    uint32_t v = read_gpio_buttons();
    v |= prg32_qemu_input_read();
    v |= prg32_diag_input_state();
#if PRG32_RESTART_HOTKEY_ENABLE
    if ((v & PRG32_RESTART_HOTKEY_P1) == PRG32_RESTART_HOTKEY_P1) {
        esp_restart();
    }
#endif
    return v & (PRG32_BTN_LEFT | PRG32_BTN_RIGHT | PRG32_BTN_UP |
                PRG32_BTN_DOWN | PRG32_BTN_A | PRG32_BTN_B |
                PRG32_BTN_SELECT);
}

const char *prg32_controller_name(uint32_t bit) {
    switch (bit) {
        case PRG32_BTN_LEFT: return "LEFT";
        case PRG32_BTN_RIGHT: return "RIGHT";
        case PRG32_BTN_UP: return "UP";
        case PRG32_BTN_DOWN: return "DOWN";
        case PRG32_BTN_A: return "A";
        case PRG32_BTN_B: return "B / BACK";
        case PRG32_BTN_START: return "SELECT";
        default: return "UNKNOWN";
    }
}
