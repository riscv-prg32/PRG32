/*
 * SETUP > MULTIPLAYER SETUP: choose the PRG32 Multiplayer Server.
 *
 * No server address is built into the firmware. This screen
 * - searches the local network (mDNS, then LAN discovery) and uses the server it
 *   finds, shown as LOCAL;
 * - saves a typed address in NVS, asking for its port separately because ':'
 *   is only on the keyboard's ascii page. A saved address is used alone;
 * - clears the saved address, so that the console finds a server on the local
 *   network by itself, and turns to the public server when it finds none.
 * See docs/usage/network.md#multiplayer-server-address.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prg32.h"
#include "prg32_config.h"
#include "prg32_multiplayer_wire.h"
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_PRG32_DISPLAY_QEMU_RGB
#define CONFIG_PRG32_DISPLAY_QEMU_RGB 0
#endif

#define MP_SETUP_TITLE "MULTIPLAYER SERVER"
/* Characters of an 8-pixel font that fit between the 8-pixel margins. */
#define MP_SETUP_LINE_CHARS 38
/* The search asks MP_SEARCH_TRIES times: mDNS, then LAN discovery listening
 * MP_SEARCH_TRY_MS. A server's beacon comes every 2 s, so 10 s hears several.
 * B is checked between tries. */
#define MP_SEARCH_TRIES 5
#define MP_SEARCH_TRY_MS 2000

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

/* Shows that a server was found on the local network and uses it on SELECT. */
static void offer_found_server(const char *found) {
  prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
  while (1) {
    if (prg32_setup_interrupted()) {
      return;
    }
    uint32_t input = prg32_input_read_menu();
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 40, "FOUND: LOCAL", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 216, "SELECT USE  B DISCARD", PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();
    if (input & MENU_ACCEPT) {
      prg32_input_wait_released(MENU_ACCEPT);
      show_message(prg32_multiplayer_use_local_server(found) == 0
                       ? "USING THE LOCAL SERVER"
                       : "NOT A SERVER ADDRESS",
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

/* SEARCH LOCAL NETWORK: mDNS, then LAN discovery, for up to 10 s. */
static void search_local_network(void) {
  if (CONFIG_PRG32_DISPLAY_QEMU_RGB) {
    show_message("QEMU CANNOT SEARCH THE LAN", 2000);
    return;
  }
  char found[PRG32_MP_URL_LEN];
  int tries = 0;
  while (tries < MP_SEARCH_TRIES) {
    if (prg32_setup_interrupted()) {
      return;
    }
    if (prg32_input_read_menu() & MENU_CANCEL) {
      prg32_input_wait_released(MENU_CANCEL);
      show_message("SEARCH STOPPED", 1000);
      return;
    }
    char progress[MP_SETUP_LINE_CHARS + 1];
    snprintf(progress, sizeof(progress), "SEARCHING THE LOCAL NETWORK %d/%d",
             tries + 1, MP_SEARCH_TRIES);
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 48, progress, PRG32_COLOR_CYAN, 0);
    prg32_gfx_text8(8, 216, "HOLD B TO STOP", PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();
    if (prg32_multiplayer_discover_mdns(found, sizeof(found)) == 0 ||
        prg32_multiplayer_discover_lan(found, sizeof(found), MP_SEARCH_TRY_MS) == 0) {
      offer_found_server(found);
      return;
    }
    tries++;
  }
  show_message("NO SERVER FOUND", 2000);
}

/* MANUAL ENTRY: an address, then its port (8081 unless changed). Typing a full
 * URL or host:port skips the port step. A saved ws://host:port is offered as
 * its two parts, any other saved URL as a whole. */
static void enter_server_url(void) {
  char host[PRG32_MP_URL_LEN] = "";
  char port[8];
  snprintf(port, sizeof(port), "%d", PRG32_MP_DEFAULT_PORT);
  char saved[PRG32_MP_URL_LEN];
  if (prg32_multiplayer_server_url_get(saved, sizeof(saved)) == 0 &&
      prg32_mp_split_server_url(saved, host, sizeof(host), port, sizeof(port)) != 0) {
    snprintf(host, sizeof(host), "%s", saved);
    snprintf(port, sizeof(port), "%d", PRG32_MP_DEFAULT_PORT);
  }
  if (prg32_text_input(host, sizeof(host), "SERVER IP OR URL") < 0 || !host[0]) {
    return;
  }
  /* Room for host, ':' and port; normalizing refuses a URL that does not fit. */
  char typed[PRG32_MP_URL_LEN + sizeof(port)];
  if (strstr(host, "://") || strchr(host, ':')) {
    snprintf(typed, sizeof(typed), "%s", host);
  } else {
    if (prg32_text_input(port, sizeof(port), "SERVER PORT") < 0) {
      return;
    }
    if (!prg32_mp_valid_port(port)) {
      show_message("NOT A PORT: USE 1 TO 65535", 2000);
      return;
    }
    snprintf(typed, sizeof(typed), "%s:%s", host, port);
  }
  char url[PRG32_MP_URL_LEN];
  if (prg32_mp_normalize_server_url(typed, url, sizeof(url)) == 0 &&
      prg32_multiplayer_server_url_set(url) == 0) {
    show_message("SAVED", 1000);
  } else {
    show_message("INVALID: USE IP, IP:PORT OR ws://", 2000);
  }
}

void prg32_setup_multiplayer_run(void) {
  enum { SEARCH, MANUAL, AUTOMATIC, BACK, ITEM_COUNT };
  static const char *const items[ITEM_COUNT] = {
      "SEARCH LOCAL NETWORK",
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
    int is_local = !has_saved && prg32_multiplayer_server_is_local();
    int has_compiled =
        !has_saved && !is_local &&
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
        if (choice == SEARCH) {
          search_local_network();
        } else if (choice == MANUAL) {
          enter_server_url();
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
      } else if (is_local) {
        prg32_gfx_text8(8, y, "SERVER:", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, y + 14, "LOCAL", PRG32_COLOR_GREEN, 0);
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
