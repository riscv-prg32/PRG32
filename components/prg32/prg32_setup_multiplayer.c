/*
 * SETUP > MULTIPLAYER SETUP: choose the PRG32 Multiplayer Server.
 *
 * No server address is built into the firmware. This screen saves one in NVS,
 * finds one with mDNS, or clears the saved one so that the console finds a
 * server on the local network by itself when a cartridge joins a room, and
 * turns to the public server when it finds none. See
 * docs/usage/network.md#multiplayer-server-address.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prg32.h"
#include "prg32_config.h"
#include "prg32_multiplayer_wire.h"
#include <stdio.h>
#include <string.h>

#define MP_SETUP_TITLE "MULTIPLAYER SERVER"
/* Characters of an 8-pixel font that fit between the 8-pixel margins. */
#define MP_SETUP_LINE_CHARS 38
#define MP_SETUP_DEFAULT_PORT 8081

static void show_message(const char *line, uint32_t ms) {
  prg32_gfx_clear(PRG32_COLOR_BLACK);
  prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
  prg32_gfx_text8(8, 48, line, PRG32_COLOR_CYAN, 0);
  prg32_gfx_present();
  vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Draws a URL over as many lines as it needs, and returns the next free y. */
static int draw_wrapped(int y, const char *text, uint16_t color) {
  char line[MP_SETUP_LINE_CHARS + 1];
  size_t len = strlen(text);
  for (size_t at = 0; at < len; at += MP_SETUP_LINE_CHARS) {
    size_t n = len - at < MP_SETUP_LINE_CHARS ? len - at : MP_SETUP_LINE_CHARS;
    memcpy(line, text + at, n);
    line[n] = '\0';
    prg32_gfx_text8(8, y, line, color, 0);
    y += 10;
  }
  return y;
}

/* A typed host gets ws:// and port 8081; a typed host:port keeps its port. */
static void complete_url(char *url, size_t cap) {
  if (!url[0] || strstr(url, "://")) {
    return;
  }
  char typed[PRG32_MP_URL_LEN];
  snprintf(typed, sizeof(typed), "%s", url);
  if (strchr(typed, ':')) {
    snprintf(url, cap, "ws://%s", typed);
  } else {
    snprintf(url, cap, "ws://%s:%d", typed, MP_SETUP_DEFAULT_PORT);
  }
}

/* Shows a server that mDNS found and saves it on SELECT. */
static void offer_found_server(const char *found) {
  prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
  while (1) {
    if (prg32_setup_interrupted()) {
      return;
    }
    uint32_t input = prg32_input_read_menu();
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 40, "FOUND:", PRG32_COLOR_GREEN, 0);
    draw_wrapped(58, found, PRG32_COLOR_WHITE);
    prg32_gfx_text8(8, 216, "SELECT SAVE  B DISCARD", PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();
    if (input & MENU_ACCEPT) {
      prg32_input_wait_released(MENU_ACCEPT);
      show_message(prg32_multiplayer_server_url_set(found) == 0 ? "SAVED"
                                                                : "SAVE FAILED",
                   1000);
      return;
    }
    if (input & MENU_CANCEL) {
      prg32_input_wait_released(MENU_CANCEL);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void prg32_setup_multiplayer_run(void) {
  enum { SCAN, MANUAL, AUTOMATIC, BACK, ITEM_COUNT };
  static const char *const items[ITEM_COUNT] = {
      "SCAN NETWORK (MDNS)",
      "MANUAL ENTRY",
      "USE AUTOMATIC DISCOVERY",
      "BACK",
  };
  int choice = 0;
  prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
  while (1) {
    char saved[PRG32_MP_URL_LEN];
    char compiled[PRG32_MP_URL_LEN];
    int has_saved = prg32_multiplayer_server_url_get(saved, sizeof(saved)) == 0;
    int has_compiled =
        !has_saved &&
        prg32_multiplayer_server_url_configured(compiled, sizeof(compiled)) == 0;
    char public_url[PRG32_MP_URL_LEN];
    int has_public =
        prg32_multiplayer_server_url_public(public_url, sizeof(public_url)) == 0;
    uint32_t last = prg32_input_read_menu();
    int redraw_settings = 0;
    while (!redraw_settings) {
      if (prg32_setup_interrupted()) {
        return;
      }
      uint32_t input = prg32_input_read_menu();
      if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
        choice--;
      }
      if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN) &&
          choice + 1 < ITEM_COUNT) {
        choice++;
      }
      if ((input & MENU_CANCEL) && !(last & MENU_CANCEL)) {
        prg32_input_wait_released(MENU_CANCEL);
        return;
      }
      if ((input & MENU_ACCEPT) && !(last & MENU_ACCEPT)) {
        prg32_input_wait_released(MENU_ACCEPT);
        if (choice == BACK) {
          return;
        }
        if (choice == SCAN) {
          char found[PRG32_MP_URL_LEN];
          if (!prg32_multiplayer_mdns_available()) {
            show_message("MDNS NOT AVAILABLE ON THIS BUILD", 2000);
          } else {
            show_message("SCANNING...", 10);
            if (prg32_multiplayer_discover_mdns(found, sizeof(found)) == 0) {
              offer_found_server(found);
            } else {
              show_message("NO SERVER FOUND", 2000);
            }
          }
        } else if (choice == MANUAL) {
          char url[PRG32_MP_URL_LEN] = "";
          if (has_saved) {
            snprintf(url, sizeof(url), "%s", saved);
          }
          if (prg32_text_input(url, sizeof(url), "SERVER URL") >= 0 && url[0]) {
            complete_url(url, sizeof(url));
            show_message(prg32_multiplayer_server_url_set(url) == 0
                             ? "SAVED"
                             : "INVALID: USE ws://HOST:PORT",
                         1500);
          }
        } else if (choice == AUTOMATIC) {
          prg32_multiplayer_server_url_clear();
          show_message("SAVED ADDRESS CLEARED", 1000);
        }
        redraw_settings = 1;
        continue;
      }
      prg32_gfx_clear(PRG32_COLOR_BLACK);
      prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
      for (int i = 0; i < ITEM_COUNT; ++i) {
        int y = 40 + i * 18;
        prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, y, items[i], PRG32_COLOR_WHITE, 0);
      }
      int y = 126;
      if (has_saved) {
        prg32_gfx_text8(8, y, "SAVED:", PRG32_COLOR_YELLOW, 0);
        draw_wrapped(y + 14, saved, PRG32_COLOR_CYAN);
      } else if (has_compiled) {
        prg32_gfx_text8(8, y, "COMPILED IN:", PRG32_COLOR_YELLOW, 0);
        draw_wrapped(y + 14, compiled, PRG32_COLOR_CYAN);
      } else {
        prg32_gfx_text8(8, y, "AUTOMATIC:", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, y + 14,
                        prg32_multiplayer_mdns_available() ? "MDNS, THEN LAN DISCOVERY"
                                                           : "LAN DISCOVERY",
                        PRG32_COLOR_CYAN, 0);
        if (has_public) {
          prg32_gfx_text8(8, y + 28, "ELSE AFTER 10 S:", PRG32_COLOR_YELLOW, 0);
          draw_wrapped(y + 42, public_url, PRG32_COLOR_CYAN);
        }
      }
      prg32_gfx_text8(8, 216, "UP/DOWN MOVE  SELECT/A OK  B BACK",
                      PRG32_COLOR_CYAN, 0);
      prg32_gfx_present();
      last = input;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}
