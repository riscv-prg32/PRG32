/*
 * SETUP > MULTIPLAYER SETUP: where the console finds its Multiplayer Server.
 * prg32_multiplayer.c keeps the settings in NVS and uses, in order, a server
 * found on the local network, the address typed here, the compiled address,
 * and the public server.
 * - SERVER URL: an IP address or URL typed with the on-screen keyboard, then its
 *   port in a second step (':' is only on the keyboard's ascii page). Used
 *   when no local server is, or always with the local server off.
 * - SEARCH LOCAL SERVER: searches now (mDNS, then LAN discovery) and shows a
 *   server it finds as LOCAL. It turns the local server on.
 * - LOCAL SERVER: ON/OFF. On, the default, a local server comes first, best
 *   for latency in a classroom; the console searches when Wi-Fi connects and
 *   when a game joins. Off, it never searches.
 * - RESET TO DEFAULT: forgets the typed address, so the fallback is the
 *   compiled address or the public server again. LOCAL SERVER is unchanged.
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

#define MP_SETUP_TITLE "MULTIPLAYER SETUP"
/* Characters of an 8-pixel font that fit between the 8-pixel margins. */
#define MP_SETUP_LINE_CHARS 38
#define MP_SETUP_LINE_HEIGHT 10
/* The search asks MP_SEARCH_TRIES times: mDNS, then LAN discovery listening
 * MP_SEARCH_TRY_MS. A server's beacon comes every 2 s, so 10 s hears several.
 * B is checked between tries. */
#define MP_SEARCH_TRIES 5
#define MP_SEARCH_TRY_MS 2000

static void show_message(const char *line1, const char *line2, uint16_t color,
                         uint32_t ms) {
  prg32_gfx_clear(PRG32_COLOR_BLACK);
  prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
  prg32_gfx_text8(8, 48, line1, color, 0);
  if (line2) {
    prg32_gfx_text8(8, 66, line2, PRG32_COLOR_CYAN, 0);
  }
  prg32_gfx_present();
  vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Draws a URL over at most `max_lines` lines, and returns the next free y. */
static int draw_wrapped(int y, const char *text, uint16_t color, int max_lines) {
  char line[MP_SETUP_LINE_CHARS + 1];
  size_t len = strlen(text);
  for (size_t at = 0; at < len && max_lines > 0;
       at += MP_SETUP_LINE_CHARS, --max_lines) {
    size_t n = len - at < MP_SETUP_LINE_CHARS ? len - at : MP_SETUP_LINE_CHARS;
    memcpy(line, text + at, n);
    line[n] = '\0';
    prg32_gfx_text8(8, y, line, color, 0);
    y += MP_SETUP_LINE_HEIGHT;
  }
  return y;
}

/* The server in use now and the fallback behind it. A server found on the
 * local network is shown as LOCAL instead of its address. */
static void draw_current_setting(int y) {
  char url[PRG32_MP_URL_LEN];
  bool local_on = prg32_multiplayer_local_server_enabled();

  prg32_gfx_text8(8, y, "SERVER:", PRG32_COLOR_YELLOW, 0);
  if (local_on && prg32_multiplayer_server_is_local()) {
    prg32_gfx_text8(72, y, "LOCAL", PRG32_COLOR_GREEN, 0);
  } else if (local_on) {
    prg32_gfx_text8(72, y, "NO LOCAL SERVER YET", PRG32_COLOR_CYAN, 0);
  } else {
    prg32_gfx_text8(72, y, "LOCAL SERVER OFF", PRG32_COLOR_CYAN, 0);
  }
  y += MP_SETUP_LINE_HEIGHT + 4;
  prg32_gfx_text8(8, y, local_on ? "ELSE:" : "USING:", PRG32_COLOR_YELLOW, 0);
  y += MP_SETUP_LINE_HEIGHT;
  if (prg32_multiplayer_server_url_configured(url, sizeof(url)) == 0) {
    draw_wrapped(y, url, PRG32_COLOR_CYAN, 3);
  } else if (prg32_multiplayer_server_url_public(url, sizeof(url)) == 0) {
    prg32_gfx_text8(8, y, local_on ? "PUBLIC SERVER AFTER 10 S:" : "PUBLIC SERVER:",
                    PRG32_COLOR_CYAN, 0);
    draw_wrapped(y + MP_SETUP_LINE_HEIGHT, url, PRG32_COLOR_CYAN, 2);
  } else {
    prg32_gfx_text8(8, y, "NONE", PRG32_COLOR_CYAN, 0);
  }
}

/* SERVER URL: an address, then its port (8081 unless changed). Typing a full
 * URL or host:port skips the port step. A saved ws://host:port is offered as
 * its two parts, any other saved URL as a whole, so a small change needs no
 * retyping. */
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
      show_message("NOT A PORT", "USE A NUMBER FROM 1 TO 65535",
                   PRG32_COLOR_YELLOW, 2500);
      return;
    }
    snprintf(typed, sizeof(typed), "%s:%s", host, port);
  }
  char url[PRG32_MP_URL_LEN];
  if (prg32_mp_normalize_server_url(typed, url, sizeof(url)) == 0 &&
      prg32_multiplayer_server_url_set(url) == 0) {
    show_message("SAVED", NULL, PRG32_COLOR_GREEN, 1000);
  } else {
    show_message("NOT A SERVER ADDRESS", "USE IP, IP:PORT OR ws://...",
                 PRG32_COLOR_YELLOW, 2500);
  }
}

/* SEARCH LOCAL SERVER: searches now, for up to 10 s, and turns the local
 * server on. Not finding one keeps it on: the console searches again when
 * Wi-Fi connects and when a game joins. */
static void search_local_network(void) {
  if (CONFIG_PRG32_DISPLAY_QEMU_RGB) {
    show_message("LOCAL SEARCH NOT AVAILABLE", "QEMU HAS NO LAN BROADCASTS",
                 PRG32_COLOR_YELLOW, 2500);
    return;
  }
  if (!prg32_multiplayer_local_server_enabled() &&
      prg32_multiplayer_set_local_server_enabled(true) != 0) {
    show_message("SAVE FAILED", NULL, PRG32_COLOR_YELLOW, 1500);
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
      show_message("SEARCH STOPPED", "SEARCHES AGAIN AT BOOT AND ON JOIN",
                   PRG32_COLOR_YELLOW, 2000);
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
      prg32_multiplayer_use_local_server(found);
      show_message("FOUND: LOCAL", NULL, PRG32_COLOR_GREEN, 1500);
      return;
    }
    tries++;
  }
  show_message("NO SERVER FOUND", "SEARCHES AGAIN AT BOOT AND ON JOIN",
               PRG32_COLOR_YELLOW, 2500);
}

void prg32_setup_multiplayer_run(void) {
  if (!prg32_multiplayer_available()) {
    show_message("MULTIPLAYER IS OFF", "IN THIS FIRMWARE BUILD",
                 PRG32_COLOR_YELLOW, 2000);
    return;
  }
  enum { MANUAL, SEARCH, LOCAL_TOGGLE, RESET, BACK, ITEM_COUNT };
  static const char *const items[ITEM_COUNT] = {
      "SERVER URL",
      "SEARCH LOCAL SERVER",
      NULL, /* LOCAL SERVER: ON or OFF, drawn from the setting */
      "RESET TO DEFAULT",
      "BACK",
  };
  int choice = 0;
  prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
  uint32_t last = prg32_input_read_menu();
  while (1) {
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
      if (choice == MANUAL) {
        enter_server_url();
      } else if (choice == SEARCH) {
        search_local_network();
      } else if (choice == LOCAL_TOGGLE) {
        bool on = !prg32_multiplayer_local_server_enabled();
        if (prg32_multiplayer_set_local_server_enabled(on) != 0) {
          show_message("SAVE FAILED", NULL, PRG32_COLOR_YELLOW, 1500);
        }
      } else if (choice == RESET) {
        prg32_multiplayer_server_url_clear();
        show_message("DEFAULT SERVER URL RESTORED", NULL, PRG32_COLOR_GREEN, 1000);
      } else {
        return;
      }
      input = prg32_input_read_menu();
    }

    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, MP_SETUP_TITLE, PRG32_COLOR_WHITE, 0);
    for (int i = 0; i < ITEM_COUNT; ++i) {
      int y = 36 + i * 18;
      prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
      const char *label = items[i];
      if (i == LOCAL_TOGGLE) {
        label = prg32_multiplayer_local_server_enabled() ? "LOCAL SERVER: ON"
                                                         : "LOCAL SERVER: OFF";
      }
      prg32_gfx_text8(24, y, label, PRG32_COLOR_WHITE, 0);
    }
    draw_current_setting(140);
    prg32_gfx_text8(8, 216, "UP/DOWN MOVE  SELECT/A OK  B BACK",
                    PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();
    last = input;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
