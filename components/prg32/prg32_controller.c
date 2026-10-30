#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

#ifndef PRG32_RESTART_HOTKEY_ENABLE
#define PRG32_RESTART_HOTKEY_ENABLE 1
#endif

#ifndef PRG32_PIN_BTN_SELECT
#define PRG32_PIN_BTN_SELECT PRG32_PIN_BTN_START
#endif

#define PRG32_RESTART_HOTKEY_P1 \
    (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_DOWN)

static const char *TAG = "prg32_controller";

uint32_t prg32_qemu_input_read(void);

/*
 * PRG32 controller layer.
 *
 * The runtime reads the reference GPIO joystick on hardware, QEMU keyboard
 * input on desktop builds, diagnostic input used by tests, and remote keys
 * sent by a developer through the device protocol.
 *
 * The game sees only the stable PRG32 bitmask: LEFT/RIGHT/UP/DOWN/SELECT/A/B.
 * This is intentionally similar to memory-mapped input registers on 1980s
 * consoles, which makes it a useful Computer Architecture teaching example.
 */

/* One entry per PRG32_BTN_* button. This table is the only firmware list of
 * remote key names: HELLO and the KEY message both read it, so a new button
 * added here reaches hosts automatically. */
static const struct {
    const char *name;
    uint32_t bit;
} REMOTE_KEYS[PRG32_REMOTE_KEY_COUNT] = {
    {"LEFT", PRG32_BTN_LEFT},
    {"RIGHT", PRG32_BTN_RIGHT},
    {"UP", PRG32_BTN_UP},
    {"DOWN", PRG32_BTN_DOWN},
    {"A", PRG32_BTN_A},
    {"B", PRG32_BTN_B},
    {"START", PRG32_BTN_START},
};

/* The main loop reads the controller once per frame just before the cartridge
 * update, so a key must survive two reads for the cartridge to get one. */
#define REMOTE_KEY_MIN_READS 2u

/* The protocol task writes this state while the main loop reads it. */
static portMUX_TYPE g_remote_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t g_remote_active;
static uint8_t g_remote_reads[PRG32_REMOTE_KEY_COUNT];
static uint32_t g_remote_release_ms[PRG32_REMOTE_KEY_COUNT];

/* A game polls once per frame, and one frame can take longer than a short
 * press. A remote key therefore stays down until its time has passed AND the
 * game has had its read, so a press is never lost between two frames. */
static uint32_t read_remote_keys(void) {
    /* Unlocked first look: nearly every read happens with no remote key. */
    if (g_remote_active == 0) {
        return 0;
    }
    uint32_t now_ms = prg32_ticks_ms();
    portENTER_CRITICAL(&g_remote_lock);
    for (int i = 0; i < PRG32_REMOTE_KEY_COUNT; ++i) {
        uint32_t bit = REMOTE_KEYS[i].bit;
        if ((g_remote_active & bit) == 0) {
            continue;
        }
        if (g_remote_reads[i] >= REMOTE_KEY_MIN_READS &&
            (int32_t)(now_ms - g_remote_release_ms[i]) >= 0) {
            g_remote_active &= ~bit;
        } else if (g_remote_reads[i] < REMOTE_KEY_MIN_READS) {
            g_remote_reads[i]++;
        }
    }
    uint32_t state = g_remote_active;
    portEXIT_CRITICAL(&g_remote_lock);
    return state;
}

const char *prg32_controller_remote_name(int index) {
    if (index < 0 || index >= PRG32_REMOTE_KEY_COUNT) {
        return NULL;
    }
    return REMOTE_KEYS[index].name;
}

uint32_t prg32_controller_remote_bit(const char *name) {
    if (!name) {
        return 0;
    }
    /* START and SELECT are two names for one button; see PRG32_BTN_SELECT. */
    if (strcmp(name, "SELECT") == 0) {
        return PRG32_BTN_SELECT;
    }
    for (int i = 0; i < PRG32_REMOTE_KEY_COUNT; ++i) {
        if (strcmp(name, REMOTE_KEYS[i].name) == 0) {
            return REMOTE_KEYS[i].bit;
        }
    }
    return 0;
}

void prg32_controller_remote_press(uint32_t mask, uint32_t hold_ms) {
    uint32_t release_ms = prg32_ticks_ms() + hold_ms;
    portENTER_CRITICAL(&g_remote_lock);
    for (int i = 0; i < PRG32_REMOTE_KEY_COUNT; ++i) {
        if (mask & REMOTE_KEYS[i].bit) {
            g_remote_release_ms[i] = release_ms;
            g_remote_reads[i] = 0;
        }
    }
    g_remote_active |= mask;
    portEXIT_CRITICAL(&g_remote_lock);
}

void prg32_controller_remote_release(uint32_t mask) {
    portENTER_CRITICAL(&g_remote_lock);
    g_remote_active &= ~mask;
    portEXIT_CRITICAL(&g_remote_lock);
}

uint32_t prg32_controller_remote_active(void) {
    return g_remote_active;
}

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
        v |= PRG32_BTN_START;
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
    v |= read_remote_keys();
#if PRG32_RESTART_HOTKEY_ENABLE
    if ((v & PRG32_RESTART_HOTKEY_P1) == PRG32_RESTART_HOTKEY_P1) {
        ESP_LOGE(TAG, "ABOUT TO esp_restart() from %s:%d\n", __FILE__, __LINE__);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
#endif
    return v;
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
