#include "prg32.h"
#include "prg32_btkbd_internal.h"
#include "prg32_config.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Setup screens for the Bluetooth keyboard (SETUP > BLUETOOTH KEYBOARD).
 *
 * Every screen follows the same loop as the other setup menus: read the
 * joystick mask, react to newly pressed bits, redraw, present, wait 80 ms.
 * When "MAP TO CONTROLS" is on, the keyboard arrows/ENTER/ESC also navigate
 * these screens because they arrive through the same PRG32_BTN_* mask.
 */

#define BTKBD_SETUP_KEYS \
  (MENU_ACCEPT | MENU_CANCEL | PRG32_BTN_UP | PRG32_BTN_DOWN | \
   PRG32_BTN_LEFT | PRG32_BTN_RIGHT)
#define BTKBD_SCAN_MS 15000u
#define BTKBD_PAIR_TIMEOUT_MS 60000u
#define BTKBD_CAPTURE_TIMEOUT_MS 10000u
#define BTKBD_FRAME_MS 80

static const char *const k_button_names[PRG32_BTKBD_BUTTON_COUNT] = {
    "LEFT", "RIGHT", "UP", "DOWN", "A", "B", "SELECT",
};

static int pressed(uint32_t input, uint32_t last, uint32_t mask) {
  return (input & mask) && !(last & mask);
}

static void draw_footer(const char *text) {
  prg32_gfx_text8(8, 228, text, PRG32_COLOR_CYAN, 0);
}

static void draw_status_lines(int y) {
  char line[48];
  char name[PRG32_BTKBD_NAME_LEN];
  snprintf(line, sizeof(line), "STATUS: %s",
           prg32_btkbd_state_name(prg32_btkbd_state()));
  prg32_gfx_text8(8, y, line, PRG32_COLOR_GREEN, 0);
  if (prg32_btkbd_ble_device_name(name, sizeof(name)) > 0) {
    snprintf(line, sizeof(line), "DEVICE: %s", name);
    prg32_gfx_text8(8, y + 12, line, PRG32_COLOR_GREEN, 0);
  }
}

/* ------------------------------------------------------------------------ */
/* Pairing                                                                  */
/* ------------------------------------------------------------------------ */

/* Show connection progress until the keyboard is ready, fails, or B. */
static void btkbd_pair_progress(const prg32_btkbd_scan_entry_t *entry) {
  prg32_btkbd_ble_connect(entry);
  uint32_t start = prg32_ticks_ms();
  uint32_t last = prg32_input_read_menu();
  int seen_activity = 0;
  while (1) {
    uint32_t input = prg32_input_read_menu();
    uint32_t state = prg32_btkbd_state();
    const char *error = prg32_btkbd_ble_last_error();
    if (state == PRG32_BTKBD_STATE_CONNECTING ||
        state == PRG32_BTKBD_STATE_PAIRING) {
      seen_activity = 1;
    }
    int finished = state == PRG32_BTKBD_STATE_CONNECTED ||
                   (state == PRG32_BTKBD_STATE_IDLE &&
                    (seen_activity || (error && error[0]))) ||
                   prg32_ticks_ms() - start > BTKBD_PAIR_TIMEOUT_MS;
    if (pressed(input, last, MENU_CANCEL) ||
        (finished && pressed(input, last, MENU_ACCEPT))) {
      prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
      return;
    }

    char line[48];
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "PAIR BLUETOOTH KEYBOARD", PRG32_COLOR_WHITE, 0);
    snprintf(line, sizeof(line), "DEVICE: %s", entry->name);
    prg32_gfx_text8(8, 32, line, PRG32_COLOR_WHITE, 0);
    snprintf(line, sizeof(line), "STATUS: %s", prg32_btkbd_state_name(state));
    prg32_gfx_text8(8, 48, line, PRG32_COLOR_GREEN, 0);

    int32_t passkey = prg32_btkbd_ble_passkey();
    if (state == PRG32_BTKBD_STATE_PAIRING && passkey >= 0) {
      snprintf(line, sizeof(line), "%06" PRId32, passkey);
      prg32_gfx_text8(8, 80, "ON THE KEYBOARD TYPE:", PRG32_COLOR_YELLOW, 0);
      prg32_gfx_rect(8, 96, 120, 20, PRG32_COLOR_BLUE);
      prg32_gfx_text8(40, 102, line, PRG32_COLOR_WHITE, PRG32_COLOR_BLUE);
      prg32_gfx_text8(8, 124, "THEN PRESS ENTER", PRG32_COLOR_YELLOW, 0);
    } else if (state == PRG32_BTKBD_STATE_CONNECTED) {
      prg32_gfx_text8(8, 80, "PAIRED! THE KEYBOARD WILL", PRG32_COLOR_GREEN, 0);
      prg32_gfx_text8(8, 92, "RECONNECT AUTOMATICALLY.", PRG32_COLOR_GREEN, 0);
    } else if (finished) {
      prg32_gfx_text8(8, 80, error && error[0] ? error : "NOT CONNECTED",
                      PRG32_COLOR_RED, 0);
      prg32_gfx_text8(8, 96, "CHECK PAIRING MODE, TRY AGAIN",
                      PRG32_COLOR_YELLOW, 0);
    } else {
      prg32_gfx_text8(8, 80, "PLEASE WAIT...", PRG32_COLOR_WHITE, 0);
    }
    draw_footer(finished ? "A/SELECT OK  B BACK" : "B CANCEL");
    prg32_gfx_present();
    last = input;
    vTaskDelay(pdMS_TO_TICKS(BTKBD_FRAME_MS));
  }
}

static void btkbd_pair_menu(void) {
  if (prg32_btkbd_ble_scan_start(BTKBD_SCAN_MS) != 0) {
    return;
  }
  int choice = 0;
  prg32_input_wait_released(BTKBD_SETUP_KEYS);
  uint32_t last = 0;
  while (1) {
    uint32_t input = prg32_input_read_menu();
    int count = prg32_btkbd_ble_scan_count();
    int scanning = prg32_btkbd_state() == PRG32_BTKBD_STATE_SCANNING;
    if (choice >= count) {
      choice = count > 0 ? count - 1 : 0;
    }
    if (pressed(input, last, PRG32_BTN_UP) && choice > 0) {
      choice--;
    }
    if (pressed(input, last, PRG32_BTN_DOWN) && choice + 1 < count) {
      choice++;
    }
    if (pressed(input, last, MENU_CANCEL)) {
      prg32_input_wait_released(MENU_CANCEL);
      break;
    }
    if (pressed(input, last, PRG32_BTN_RIGHT) && !scanning) {
      prg32_btkbd_ble_scan_start(BTKBD_SCAN_MS); /* scan again */
    }
    if (pressed(input, last, MENU_ACCEPT) && count > 0) {
      prg32_btkbd_scan_entry_t entry;
      if (prg32_btkbd_ble_scan_get(choice, &entry) == 0) {
        prg32_input_wait_released(MENU_ACCEPT);
        btkbd_pair_progress(&entry);
        break;
      }
    }

    char line[48];
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "PAIR BLUETOOTH KEYBOARD", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 24, "PUT THE KEYBOARD IN PAIRING MODE",
                    PRG32_COLOR_YELLOW, 0);
    prg32_gfx_text8(8, 40, scanning ? "SCANNING..." : "SCAN FINISHED",
                    PRG32_COLOR_GREEN, 0);
    if (count == 0 && !scanning) {
      prg32_gfx_text8(8, 64, "NO KEYBOARD FOUND", PRG32_COLOR_RED, 0);
    }
    for (int i = 0; i < count; ++i) {
      prg32_btkbd_scan_entry_t entry;
      if (prg32_btkbd_ble_scan_get(i, &entry) != 0) {
        continue;
      }
      int y = 64 + i * 16;
      snprintf(line, sizeof(line), "%-26.26s %4d", entry.name, entry.rssi);
      prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
      prg32_gfx_text8(24, y, line, PRG32_COLOR_WHITE, 0);
    }
    draw_footer(scanning ? "SELECT/A PAIR  B BACK"
                         : "SELECT/A PAIR  RIGHT RESCAN  B BACK");
    prg32_gfx_present();
    last = input;
    vTaskDelay(pdMS_TO_TICKS(BTKBD_FRAME_MS));
  }
  prg32_btkbd_ble_scan_stop();
}

/* ------------------------------------------------------------------------ */
/* Mapping editor                                                           */
/* ------------------------------------------------------------------------ */

/* Wait for one key on the Bluetooth keyboard and return its HID usage,
 * 0 for "no key" (BACKSPACE on the alternate slot), or -1 when cancelled. */
static int btkbd_capture_usage(const char *button, int slot) {
  prg32_input_wait_released(BTKBD_SETUP_KEYS);
  prg32_btkbd_flush();
  uint32_t start = prg32_ticks_ms();
  uint32_t last = prg32_input_read_menu();
  while (prg32_ticks_ms() - start < BTKBD_CAPTURE_TIMEOUT_MS) {
    prg32_btkbd_event_t event;
    while (prg32_btkbd_read_event(&event)) {
      if (event.usage == PRG32_HID_KEY_BACKSPACE && slot == 1) {
        return 0;
      }
      if (event.usage != 0) {
        return event.usage;
      }
    }
    uint32_t input = prg32_input_read_menu();
    if (pressed(input, last, MENU_CANCEL)) {
      prg32_input_wait_released(MENU_CANCEL);
      return -1;
    }

    char line[48];
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "CUSTOMIZE KEY MAPPING", PRG32_COLOR_WHITE, 0);
    snprintf(line, sizeof(line), "PRESS THE %s KEY FOR %s",
             slot == 0 ? "PRIMARY" : "ALTERNATE", button);
    prg32_gfx_text8(8, 48, line, PRG32_COLOR_YELLOW, 0);
    if (slot == 1) {
      prg32_gfx_text8(8, 64, "BACKSPACE = NO ALTERNATE KEY",
                      PRG32_COLOR_CYAN, 0);
    }
    snprintf(line, sizeof(line), "%" PRIu32 " S LEFT",
             (BTKBD_CAPTURE_TIMEOUT_MS - (prg32_ticks_ms() - start)) / 1000u);
    prg32_gfx_text8(8, 88, line, PRG32_COLOR_GREEN, 0);
    draw_footer("JOYSTICK B CANCEL");
    prg32_gfx_present();
    last = input;
    vTaskDelay(pdMS_TO_TICKS(BTKBD_FRAME_MS));
  }
  return -1;
}

static void btkbd_customize_menu(void) {
  const int rows = PRG32_BTKBD_BUTTON_COUNT + 1; /* buttons + BACK */
  int choice = 0;
  prg32_input_wait_released(BTKBD_SETUP_KEYS);
  uint32_t last = 0;
  while (1) {
    uint32_t input = prg32_input_read_menu();
    if (pressed(input, last, PRG32_BTN_UP) && choice > 0) {
      choice--;
    }
    if (pressed(input, last, PRG32_BTN_DOWN) && choice + 1 < rows) {
      choice++;
    }
    if (pressed(input, last, MENU_CANCEL)) {
      prg32_input_wait_released(MENU_CANCEL);
      return;
    }
    if (pressed(input, last, MENU_ACCEPT)) {
      if (choice == rows - 1) {
        prg32_input_wait_released(MENU_ACCEPT);
        return;
      }
      /* Suspend the mapping while capturing, so the pressed key only
       * becomes a mapping and does not also move the menu cursor. */
      int previous = prg32_btkbd_set_mapping(0);
      int primary = btkbd_capture_usage(k_button_names[choice], 0);
      int alternate = primary > 0
                          ? btkbd_capture_usage(k_button_names[choice], 1)
                          : -1;
      prg32_btkbd_set_mapping(previous);
      if (primary > 0 && alternate >= 0) {
        prg32_btkbd_map_t map;
        prg32_btkbd_map_get(&map);
        map.usage[choice][0] = (uint8_t)primary;
        map.usage[choice][1] = (uint8_t)alternate;
        prg32_btkbd_map_set(&map);
      }
      prg32_input_wait_released(BTKBD_SETUP_KEYS);
      last = prg32_input_read_menu();
      continue;
    }

    prg32_btkbd_map_t map;
    prg32_btkbd_map_get(&map);
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "CUSTOMIZE KEY MAPPING", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(24, 28, "BUTTON   PRIMARY  ALTERNATE", PRG32_COLOR_CYAN, 0);
    for (int i = 0; i < rows; ++i) {
      int y = 44 + i * 16;
      char line[48];
      if (i < PRG32_BTKBD_BUTTON_COUNT) {
        char primary[12];
        char alternate[12];
        snprintf(line, sizeof(line), "%-8s %-8s %s", k_button_names[i],
                 prg32_btkbd_usage_name(map.usage[i][0], primary,
                                        sizeof(primary)),
                 prg32_btkbd_usage_name(map.usage[i][1], alternate,
                                        sizeof(alternate)));
      } else {
        snprintf(line, sizeof(line), "BACK");
      }
      prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
      prg32_gfx_text8(24, y, line, PRG32_COLOR_WHITE, 0);
    }
    draw_footer("SELECT/A CHANGE  B BACK");
    prg32_gfx_present();
    last = input;
    vTaskDelay(pdMS_TO_TICKS(BTKBD_FRAME_MS));
  }
}

/* ------------------------------------------------------------------------ */
/* Main keyboard setup screen                                               */
/* ------------------------------------------------------------------------ */

typedef enum {
  BTKBD_ROW_PAIR,
  BTKBD_ROW_FORGET,
  BTKBD_ROW_MAPPING,
  BTKBD_ROW_CUSTOMIZE,
  BTKBD_ROW_RESET,
  BTKBD_ROW_BACK,
  BTKBD_ROW_COUNT,
} btkbd_row_t;

void prg32_setup_btkbd_run(void) {
  int choice = prg32_btkbd_ble_available() ? BTKBD_ROW_PAIR : BTKBD_ROW_MAPPING;
  char typed[33] = "";
  prg32_input_wait_released(BTKBD_SETUP_KEYS);
  prg32_btkbd_flush();
  uint32_t last = 0;
  while (1) {
    uint32_t input = prg32_input_read_menu();

    /* Typing test: show the characters received from the keyboard. */
    prg32_btkbd_event_t event;
    while (prg32_btkbd_read_event(&event)) {
      size_t length = strlen(typed);
      if (event.code == PRG32_KEY_BACKSPACE && length > 0) {
        typed[length - 1] = '\0';
      } else if (event.code >= 32 && event.code < 127) {
        if (length + 1 >= sizeof(typed)) {
          memmove(typed, typed + 1, length);
          length--;
        }
        typed[length] = (char)event.code;
        typed[length + 1] = '\0';
      }
    }

    if (pressed(input, last, PRG32_BTN_UP) && choice > 0) {
      choice--;
    }
    if (pressed(input, last, PRG32_BTN_DOWN) && choice + 1 < BTKBD_ROW_COUNT) {
      choice++;
    }
    if (pressed(input, last, MENU_CANCEL)) {
      prg32_input_wait_released(MENU_CANCEL);
      return;
    }
    int toggle = pressed(input, last, PRG32_BTN_LEFT | PRG32_BTN_RIGHT);
    if (pressed(input, last, MENU_ACCEPT) || toggle) {
      prg32_input_wait_released(BTKBD_SETUP_KEYS);
      switch (choice) {
        case BTKBD_ROW_PAIR:
          if (!toggle) {
            btkbd_pair_menu();
          }
          break;
        case BTKBD_ROW_FORGET:
          if (!toggle) {
            prg32_btkbd_ble_forget();
          }
          break;
        case BTKBD_ROW_MAPPING:
          prg32_btkbd_mapping_set_enabled(!prg32_btkbd_mapping_enabled());
          break;
        case BTKBD_ROW_CUSTOMIZE:
          if (!toggle) {
            btkbd_customize_menu();
          }
          break;
        case BTKBD_ROW_RESET:
          if (!toggle) {
            prg32_btkbd_map_reset();
          }
          break;
        default:
          if (!toggle) {
            return;
          }
          break;
      }
      prg32_input_wait_released(BTKBD_SETUP_KEYS);
      prg32_btkbd_flush();
      last = prg32_input_read_menu();
      continue;
    }

    char line[48];
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "BLUETOOTH KEYBOARD", PRG32_COLOR_WHITE, 0);
    draw_status_lines(28);
    if (!prg32_btkbd_ble_available()) {
      prg32_gfx_text8(8, 40, prg32_btkbd_ble_last_error(), PRG32_COLOR_RED, 0);
    }
    snprintf(line, sizeof(line), "TEST: %s_", typed);
    prg32_gfx_text8(8, 56, line, PRG32_COLOR_YELLOW, 0);

    for (int i = 0; i < BTKBD_ROW_COUNT; ++i) {
      int y = 80 + i * 16;
      switch (i) {
        case BTKBD_ROW_PAIR: snprintf(line, sizeof(line), "PAIR NEW KEYBOARD"); break;
        case BTKBD_ROW_FORGET: snprintf(line, sizeof(line), "FORGET KEYBOARD"); break;
        case BTKBD_ROW_MAPPING:
          snprintf(line, sizeof(line), "MAP TO CONTROLS: %s",
                   prg32_btkbd_mapping_enabled() ? "ON" : "OFF");
          break;
        case BTKBD_ROW_CUSTOMIZE: snprintf(line, sizeof(line), "CUSTOMIZE MAPPING"); break;
        case BTKBD_ROW_RESET: snprintf(line, sizeof(line), "RESET DEFAULT MAPPING"); break;
        default: snprintf(line, sizeof(line), "BACK"); break;
      }
      prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
      prg32_gfx_text8(24, y, line, PRG32_COLOR_WHITE, 0);
    }
    draw_footer("UP/DOWN MOVE  SELECT/A OK  B BACK");
    prg32_gfx_present();
    last = input;
    vTaskDelay(pdMS_TO_TICKS(BTKBD_FRAME_MS));
  }
}
