#include "prg32.h"
#include "prg32_btkbd_internal.h"
#include "prg32_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

/*
 * PRG32 keyboard logic.
 *
 * A Bluetooth LE keyboard in "boot protocol" sends one 8-byte input report
 * every time the set of held keys changes:
 *
 *   byte 0     modifier bits (LCTRL, LSHIFT, LALT, LGUI, RCTRL, ...)
 *   byte 1     reserved
 *   bytes 2..7 up to six HID usages of the keys currently held down
 *
 * The report describes a *state*, not an event. This file compares every new
 * report with the previous one to find newly pressed keys, turns each of them
 * into a character or PRG32_KEY_* code, and appends it to a small ring buffer.
 * Cartridges drain that buffer with prg32_btkbd_read_key().
 *
 * The same held-key state also drives the optional joystick mapping: when a
 * key assigned to a PRG32 button is down, prg32_btkbd_buttons() reports that
 * button bit, so every existing game can be played from the keyboard.
 *
 * The radio code lives in prg32_btkbd_ble.c and calls
 * prg32_btkbd_on_boot_report() from the NimBLE host task, while cartridges run
 * in the main task. A FreeRTOS critical section protects the shared state.
 */

static const char *TAG = "prg32_btkbd";

#define BTKBD_QUEUE_LEN 32u
#define BTKBD_REPORT_KEYS 6
#define BTKBD_REPEAT_DELAY_MS 500u
#define BTKBD_REPEAT_PERIOD_MS 60u
#define BTKBD_INJECT_HOLD_MS 120u

#define BTKBD_NVS_NAMESPACE "prg32"
#define BTKBD_NVS_MAP "kbd_map"
#define BTKBD_NVS_MAP_ON "kbd_map_on"

/* HID usage range of the eight modifier keys (LCTRL .. RGUI). */
#define HID_USAGE_MODIFIER_FIRST 0xE0u
#define HID_USAGE_MODIFIER_LAST 0xE7u
#define HID_USAGE_CAPS_LOCK 0x39u
#define HID_USAGE_ERROR_ROLLOVER 0x01u

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* Ring buffer of translated key events. head == tail means empty. */
static prg32_btkbd_event_t s_queue[BTKBD_QUEUE_LEN];
static uint32_t s_head;
static uint32_t s_tail;

/* Last boot report, used to find which keys changed. */
static uint8_t s_modifiers;
static uint8_t s_held[BTKBD_REPORT_KEYS];
static int s_caps_lock;

/* Typematic repeat of the most recently pressed key. */
static uint16_t s_repeat_code;
static uint8_t s_repeat_usage;
static uint32_t s_repeat_start_ms;
static uint32_t s_repeat_last_ms;

/* Console-injected keys (QEMU) stay "held" for a short time. */
static uint8_t s_inject_usage;
static uint32_t s_inject_until_ms;

/* Keyboard-to-joystick mapping. */
static prg32_btkbd_map_t s_map;
static int s_map_enabled = 1;
static int s_session_map_enabled = 1;

/* Default mapping: arrows and WASD move, Z/J = A, X/ESC = B,
 * ENTER/SPACE = SELECT. The same keys drive the QEMU console mapper. */
static void btkbd_default_map(prg32_btkbd_map_t *map) {
  static const uint8_t defaults[PRG32_BTKBD_BUTTON_COUNT]
                               [PRG32_BTKBD_KEYS_PER_BUTTON] = {
      {PRG32_HID_KEY_LEFT, PRG32_HID_KEY_LETTER('A')},  /* LEFT   */
      {PRG32_HID_KEY_RIGHT, PRG32_HID_KEY_LETTER('D')}, /* RIGHT  */
      {PRG32_HID_KEY_UP, PRG32_HID_KEY_LETTER('W')},    /* UP     */
      {PRG32_HID_KEY_DOWN, PRG32_HID_KEY_LETTER('S')},  /* DOWN   */
      {PRG32_HID_KEY_LETTER('Z'), PRG32_HID_KEY_LETTER('J')}, /* A */
      {PRG32_HID_KEY_LETTER('X'), PRG32_HID_KEY_ESCAPE},      /* B */
      {PRG32_HID_KEY_ENTER, PRG32_HID_KEY_SPACE},        /* SELECT */
  };
  memcpy(map->usage, defaults, sizeof(map->usage));
}

/* ------------------------------------------------------------------------ */
/* HID usage -> character translation (US layout)                           */
/* ------------------------------------------------------------------------ */

/* Usages 0x1E..0x38 in order: digits, ENTER, ESC, BACKSPACE, TAB, SPACE and
 * punctuation. Letters (0x04..0x1D) are computed instead of tabulated. */
static const char k_row_normal[] = "1234567890\n\x1b\b\t -=[]\\#;'`,./";
static const char k_row_shifted[] = "!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";

/* Numeric keypad usages 0x54..0x63. */
static const char k_keypad[] = "/*-+\n1234567890.";

static uint16_t btkbd_translate(uint8_t usage, uint8_t modifiers) {
  int shift = (modifiers & PRG32_KEYMOD_SHIFT) != 0;
  int ctrl = (modifiers & PRG32_KEYMOD_CTRL) != 0;

  if (usage >= 0x04u && usage <= 0x1Du) {
    char letter = (char)('a' + (usage - 0x04u));
    if (ctrl) {
      return (uint16_t)(letter & 0x1f); /* CTRL+A = 1 ... CTRL+Z = 26 */
    }
    if (shift != s_caps_lock) {
      letter = (char)(letter - 'a' + 'A');
    }
    return (uint16_t)(uint8_t)letter;
  }
  if (usage >= 0x1Eu && usage <= 0x38u) {
    const char *row = shift ? k_row_shifted : k_row_normal;
    return (uint16_t)(uint8_t)row[usage - 0x1Eu];
  }
  if (usage >= 0x3Au && usage <= 0x45u) {
    return (uint16_t)(PRG32_KEY_F1 + (usage - 0x3Au));
  }
  if (usage >= 0x54u && usage <= 0x63u) {
    return (uint16_t)(uint8_t)k_keypad[usage - 0x54u];
  }
  switch (usage) {
    case 0x49: return PRG32_KEY_INSERT;
    case 0x4A: return PRG32_KEY_HOME;
    case 0x4B: return PRG32_KEY_PAGE_UP;
    case 0x4C: return PRG32_KEY_DELETE;
    case 0x4D: return PRG32_KEY_END;
    case 0x4E: return PRG32_KEY_PAGE_DOWN;
    case 0x4F: return PRG32_KEY_RIGHT;
    case 0x50: return PRG32_KEY_LEFT;
    case 0x51: return PRG32_KEY_DOWN;
    case 0x52: return PRG32_KEY_UP;
    default: return 0;
  }
}

/* ------------------------------------------------------------------------ */
/* Event queue                                                              */
/* ------------------------------------------------------------------------ */

/* Caller holds s_lock. When the queue is full the oldest event is dropped,
 * so a cartridge that never reads keys cannot block the radio task. */
static void btkbd_push_locked(uint16_t code, uint8_t usage, uint8_t modifiers) {
  uint32_t next = (s_head + 1u) % BTKBD_QUEUE_LEN;
  if (next == s_tail) {
    s_tail = (s_tail + 1u) % BTKBD_QUEUE_LEN;
  }
  s_queue[s_head].code = code;
  s_queue[s_head].usage = usage;
  s_queue[s_head].modifiers = modifiers;
  s_head = next;
}

static int btkbd_report_has(const uint8_t *keys, uint8_t usage) {
  for (int i = 0; i < BTKBD_REPORT_KEYS; ++i) {
    if (keys[i] == usage) {
      return 1;
    }
  }
  return 0;
}

void prg32_btkbd_on_boot_report(const uint8_t *report, size_t length) {
  if (!report || length < 8u) {
    return;
  }
  const uint8_t modifiers = report[0];
  const uint8_t *keys = &report[2];

  /* "Phantom" state: the keyboard reports usage 0x01 in every slot when too
   * many keys are held. Keep the previous state until it recovers. */
  if (keys[0] == HID_USAGE_ERROR_ROLLOVER) {
    return;
  }

  uint32_t now = prg32_ticks_ms();
  taskENTER_CRITICAL(&s_lock);

  /* A modifier going down is also an event, so the mapping editor can
   * assign CTRL or SHIFT to a button. It produces no character. */
  uint8_t new_modifiers = (uint8_t)(modifiers & ~s_modifiers);
  for (int bit = 0; bit < 8; ++bit) {
    if (new_modifiers & (1u << bit)) {
      btkbd_push_locked(0, (uint8_t)(HID_USAGE_MODIFIER_FIRST + bit),
                        modifiers);
    }
  }

  for (int i = 0; i < BTKBD_REPORT_KEYS; ++i) {
    uint8_t usage = keys[i];
    if (usage == 0 || btkbd_report_has(s_held, usage)) {
      continue; /* empty slot or key that was already down */
    }
    if (usage == HID_USAGE_CAPS_LOCK) {
      s_caps_lock = !s_caps_lock;
    }
    uint16_t code = btkbd_translate(usage, modifiers);
    btkbd_push_locked(code, usage, modifiers);
    if (code != 0) {
      s_repeat_code = code;
      s_repeat_usage = usage;
      s_repeat_start_ms = now;
      s_repeat_last_ms = now;
    }
  }

  if (s_repeat_usage && !btkbd_report_has(keys, s_repeat_usage)) {
    s_repeat_code = 0;
    s_repeat_usage = 0;
  }
  s_modifiers = modifiers;
  memcpy(s_held, keys, sizeof(s_held));
  taskEXIT_CRITICAL(&s_lock);
}

void prg32_btkbd_on_disconnect(void) {
  taskENTER_CRITICAL(&s_lock);
  s_modifiers = 0;
  memset(s_held, 0, sizeof(s_held));
  s_repeat_code = 0;
  s_repeat_usage = 0;
  taskEXIT_CRITICAL(&s_lock);
}

void prg32_btkbd_inject_key(int code, uint8_t usage) {
  taskENTER_CRITICAL(&s_lock);
  btkbd_push_locked((uint16_t)code, usage, 0);
  s_inject_usage = usage;
  s_inject_until_ms = prg32_ticks_ms() + BTKBD_INJECT_HOLD_MS;
  taskEXIT_CRITICAL(&s_lock);
}

int prg32_btkbd_read_event(prg32_btkbd_event_t *out) {
  int found = 0;
  uint32_t now = prg32_ticks_ms();
  taskENTER_CRITICAL(&s_lock);
  if (s_head != s_tail) {
    *out = s_queue[s_tail];
    s_tail = (s_tail + 1u) % BTKBD_QUEUE_LEN;
    found = 1;
  } else if (s_repeat_code != 0 &&
             now - s_repeat_start_ms >= BTKBD_REPEAT_DELAY_MS &&
             now - s_repeat_last_ms >= BTKBD_REPEAT_PERIOD_MS) {
    /* Keyboards do not repeat keys by themselves: the host does. */
    s_repeat_last_ms = now;
    out->code = s_repeat_code;
    out->usage = s_repeat_usage;
    out->modifiers = s_modifiers;
    found = 1;
  }
  taskEXIT_CRITICAL(&s_lock);
  return found;
}

/* ------------------------------------------------------------------------ */
/* Held keys and joystick mapping                                           */
/* ------------------------------------------------------------------------ */

/* Caller holds s_lock. */
static int btkbd_usage_down_locked(uint8_t usage) {
  if (usage == 0) {
    return 0;
  }
  if (usage >= HID_USAGE_MODIFIER_FIRST && usage <= HID_USAGE_MODIFIER_LAST) {
    return (s_modifiers & (1u << (usage - HID_USAGE_MODIFIER_FIRST))) != 0;
  }
  if (usage == s_inject_usage &&
      (int32_t)(s_inject_until_ms - prg32_ticks_ms()) > 0) {
    return 1;
  }
  return btkbd_report_has(s_held, usage);
}

uint32_t prg32_btkbd_buttons(void) {
  if (!s_map_enabled || !s_session_map_enabled) {
    return 0;
  }
  uint32_t mask = 0;
  taskENTER_CRITICAL(&s_lock);
  for (int button = 0; button < PRG32_BTKBD_BUTTON_COUNT; ++button) {
    for (int k = 0; k < PRG32_BTKBD_KEYS_PER_BUTTON; ++k) {
      if (btkbd_usage_down_locked(s_map.usage[button][k])) {
        mask |= 1u << button; /* row order equals PRG32_BTN_* bit order */
      }
    }
  }
  taskEXIT_CRITICAL(&s_lock);
  return mask;
}

int prg32_btkbd_mapping_enabled(void) {
  return s_map_enabled;
}

int prg32_btkbd_session_mapping_enabled(void) {
  return s_session_map_enabled;
}

void prg32_btkbd_session_reset(void) {
  s_session_map_enabled = 1;
  prg32_btkbd_flush();
}

static void btkbd_save_settings(void) {
  nvs_handle_t nvs;
  if (nvs_open(BTKBD_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
    ESP_LOGW(TAG, "cannot open NVS to save keyboard mapping");
    return;
  }
  nvs_set_blob(nvs, BTKBD_NVS_MAP, s_map.usage, sizeof(s_map.usage));
  nvs_set_u8(nvs, BTKBD_NVS_MAP_ON, (uint8_t)(s_map_enabled ? 1 : 0));
  nvs_commit(nvs);
  nvs_close(nvs);
}

static void btkbd_load_settings(void) {
  btkbd_default_map(&s_map);
  s_map_enabled = 1;
  nvs_handle_t nvs;
  if (nvs_open(BTKBD_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
    return;
  }
  prg32_btkbd_map_t stored;
  size_t size = sizeof(stored.usage);
  if (nvs_get_blob(nvs, BTKBD_NVS_MAP, stored.usage, &size) == ESP_OK &&
      size == sizeof(stored.usage)) {
    s_map = stored;
  }
  uint8_t enabled = 1;
  if (nvs_get_u8(nvs, BTKBD_NVS_MAP_ON, &enabled) == ESP_OK) {
    s_map_enabled = enabled ? 1 : 0;
  }
  nvs_close(nvs);
}

void prg32_btkbd_mapping_set_enabled(int enabled) {
  s_map_enabled = enabled ? 1 : 0;
  btkbd_save_settings();
}

void prg32_btkbd_map_get(prg32_btkbd_map_t *out) {
  if (out) {
    taskENTER_CRITICAL(&s_lock);
    *out = s_map;
    taskEXIT_CRITICAL(&s_lock);
  }
}

void prg32_btkbd_map_set(const prg32_btkbd_map_t *map) {
  if (!map) {
    return;
  }
  taskENTER_CRITICAL(&s_lock);
  s_map = *map;
  taskEXIT_CRITICAL(&s_lock);
  btkbd_save_settings();
}

void prg32_btkbd_map_reset(void) {
  prg32_btkbd_map_t map;
  btkbd_default_map(&map);
  prg32_btkbd_map_set(&map);
}

/* ------------------------------------------------------------------------ */
/* Names for setup screens                                                  */
/* ------------------------------------------------------------------------ */

const char *prg32_btkbd_usage_name(uint8_t usage, char *buffer,
                                   size_t capacity) {
  static const char *const modifier_names[8] = {
      "LCTRL", "LSHIFT", "LALT", "LGUI", "RCTRL", "RSHIFT", "RALT", "RGUI",
  };
  if (!buffer || capacity == 0) {
    return "";
  }
  if (usage == 0) {
    snprintf(buffer, capacity, "---");
  } else if (usage >= HID_USAGE_MODIFIER_FIRST &&
             usage <= HID_USAGE_MODIFIER_LAST) {
    snprintf(buffer, capacity, "%s",
             modifier_names[usage - HID_USAGE_MODIFIER_FIRST]);
  } else if (usage >= 0x3Au && usage <= 0x45u) {
    snprintf(buffer, capacity, "F%d", usage - 0x3Au + 1);
  } else {
    switch (usage) {
      case PRG32_HID_KEY_ENTER: snprintf(buffer, capacity, "ENTER"); break;
      case PRG32_HID_KEY_ESCAPE: snprintf(buffer, capacity, "ESC"); break;
      case PRG32_HID_KEY_BACKSPACE: snprintf(buffer, capacity, "BKSP"); break;
      case PRG32_HID_KEY_TAB: snprintf(buffer, capacity, "TAB"); break;
      case PRG32_HID_KEY_SPACE: snprintf(buffer, capacity, "SPACE"); break;
      case PRG32_HID_KEY_RIGHT: snprintf(buffer, capacity, "RIGHT"); break;
      case PRG32_HID_KEY_LEFT: snprintf(buffer, capacity, "LEFT"); break;
      case PRG32_HID_KEY_DOWN: snprintf(buffer, capacity, "DOWN"); break;
      case PRG32_HID_KEY_UP: snprintf(buffer, capacity, "UP"); break;
      default: {
        uint16_t code = btkbd_translate(usage, PRG32_KEYMOD_LSHIFT);
        if (code > 32u && code < 127u) {
          snprintf(buffer, capacity, "%c", (char)code);
        } else {
          snprintf(buffer, capacity, "0x%02X", usage);
        }
        break;
      }
    }
  }
  return buffer;
}

const char *prg32_btkbd_state_name(uint32_t state) {
  switch (state) {
    case PRG32_BTKBD_STATE_IDLE: return "NOT CONNECTED";
    case PRG32_BTKBD_STATE_SCANNING: return "SCANNING";
    case PRG32_BTKBD_STATE_CONNECTING: return "CONNECTING";
    case PRG32_BTKBD_STATE_PAIRING: return "PAIRING";
    case PRG32_BTKBD_STATE_CONNECTED: return "CONNECTED";
    case PRG32_BTKBD_STATE_CONSOLE: return "CONSOLE KEYBOARD";
    default: return "UNAVAILABLE";
  }
}

/* ------------------------------------------------------------------------ */
/* Service start                                                            */
/* ------------------------------------------------------------------------ */

void prg32_btkbd_init(void) {
  btkbd_load_settings();
  if (prg32_btkbd_ble_init() != 0) {
    ESP_LOGI(TAG, "Bluetooth keyboard transport not available in this build");
  }
}

/* ------------------------------------------------------------------------ */
/* Cartridge ABI (prg32_abi.json indices 139..145)                          */
/* ------------------------------------------------------------------------ */

uint32_t prg32_btkbd_state(void) {
  if (prg32_btkbd_ble_available()) {
    return prg32_btkbd_ble_state();
  }
#if CONFIG_PRG32_QEMU_UART_INPUT
  return PRG32_BTKBD_STATE_CONSOLE;
#else
  return PRG32_BTKBD_STATE_UNAVAILABLE;
#endif
}

uint32_t prg32_qemu_input_read(void);

int prg32_btkbd_read_key(void) {
  /* On QEMU the console is drained by the input reader; poll it here too so
   * a cartridge that only reads keys still receives characters. */
  (void)prg32_qemu_input_read();
  prg32_btkbd_event_t event;
  while (prg32_btkbd_read_event(&event)) {
    if (event.code != 0) {
      return (int)event.code;
    }
  }
  return 0;
}

uint32_t prg32_btkbd_modifiers(void) {
  return s_modifiers;
}

int prg32_btkbd_key_down(uint32_t hid_usage) {
  if (hid_usage > 0xFFu) {
    return 0;
  }
  taskENTER_CRITICAL(&s_lock);
  int down = btkbd_usage_down_locked((uint8_t)hid_usage);
  taskEXIT_CRITICAL(&s_lock);
  return down;
}

void prg32_btkbd_flush(void) {
  taskENTER_CRITICAL(&s_lock);
  s_head = 0;
  s_tail = 0;
  s_repeat_code = 0;
  s_repeat_usage = 0;
  taskEXIT_CRITICAL(&s_lock);
}

int prg32_btkbd_set_mapping(int enabled) {
  int previous = s_session_map_enabled;
  s_session_map_enabled = enabled ? 1 : 0;
  return previous;
}

int prg32_btkbd_device_name(char *buffer, size_t capacity) {
  if (!buffer || capacity == 0) {
    return -1;
  }
  buffer[0] = '\0';
  if (prg32_btkbd_state() == PRG32_BTKBD_STATE_CONSOLE) {
    snprintf(buffer, capacity, "console");
    return (int)strlen(buffer);
  }
  return prg32_btkbd_ble_device_name(buffer, capacity);
}
