#include "prg32.h"
#include "prg32_config.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prg32.h"
#include "prg32_config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_ACCEPT (PRG32_BTN_SELECT | PRG32_BTN_B)
#define STORE_CANCEL PRG32_BTN_A
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#define STORE_MAX_GAMES 40
#else
#define STORE_MAX_GAMES 64
#endif
#define STORE_PAGE_SIZE 4

typedef struct {
  char id[48];
  char title[32];
  char version[16];
  char summary[96];
  char arch[32];
  char tags[48];
} store_game_t;

static const char *TAG = "prg32_setup_store";
static char *catalog_body;
static store_game_t *games;
static int game_count;

static int store_buffers_alloc(char *status, size_t status_len) {
    if (catalog_body && games) {
        return 0;
    }
    catalog_body = heap_caps_calloc(1,
                                    PRG32_STORE_CATALOG_MAX_BYTES,
                                    MALLOC_CAP_8BIT);
    games = heap_caps_calloc(STORE_MAX_GAMES,
                             sizeof(store_game_t),
                             MALLOC_CAP_8BIT);
    if (!catalog_body || !games) {
        heap_caps_free(catalog_body);
        heap_caps_free(games);
        catalog_body = NULL;
        games = NULL;
        snprintf(status, status_len, "NO MEM");
        return -1;
    }
    return 0;
}

static void store_buffers_free(void) {
    heap_caps_free(catalog_body);
    heap_caps_free(games);
    catalog_body = NULL;
    games = NULL;
    game_count = 0;
}

static void wait_and_show(const char *line, uint32_t ms) {
  prg32_gfx_clear(PRG32_COLOR_BLACK);
  prg32_gfx_text8(8, 8, "CARTRIDGE STORE", PRG32_COLOR_WHITE, 0);
  prg32_gfx_text8(8, 48, line, PRG32_COLOR_CYAN, 0);
  prg32_gfx_present();
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static void normalize_store_url(char *url, size_t cap) {
  if (!url || cap == 0 || strstr(url, "://")) {
    return;
  }
  char tmp[PRG32_STORE_URL_MAX_LEN];
  snprintf(tmp, sizeof(tmp), "http://%s:%d", url, PRG32_STORE_DEFAULT_PORT);
  snprintf(url, cap, "%s", tmp);
}

static int json_string_after(const char *start, const char *end,
                             const char *key, char *out, size_t cap) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *p = start;
  out[0] = '\0';
  while (p && p < end) {
    p = strstr(p, needle);
    if (!p || p >= end) {
      return -1;
    }
    p = strchr(p + strlen(needle), ':');
    if (!p || p >= end) {
      return -1;
    }
    p++;
    while (p < end && isspace((unsigned char)*p)) {
      p++;
    }
    if (p < end && *p == '"') {
      p++;
      size_t i = 0;
      while (p < end && *p && *p != '"' && i + 1 < cap) {
        out[i++] = *p++;
      }
      out[i] = '\0';
      return out[0] ? 0 : -1;
    }
  }
  return -1;
}

static int json_arches_after(const char *start, const char *end, char *out,
                             size_t cap) {
  const char *p = strstr(start, "\"architectures\"");
  if (!p || p >= end) {
    return -1;
  }
  p = strchr(p, '[');
  const char *q = p ? strchr(p, ']') : NULL;
  if (!p || !q || q >= end) {
    return -1;
  }
  size_t i = 0;
  while (p < q && i + 1 < cap) {
    if (*p == '"') {
      p++;
      if (i && i + 2 < cap) {
        out[i++] = ',';
        out[i++] = ' ';
      }
      while (p < q && *p != '"' && i + 1 < cap) {
        out[i++] = *p++;
      }
    }
    p++;
  }
  out[i] = '\0';
  return out[0] ? 0 : -1;
}

static int json_array_strings_after(const char *start, const char *end,
                                    const char *key, char *out, size_t cap) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *p = strstr(start, needle);
  if (!p || p >= end) {
    return -1;
  }
  p = strchr(p, '[');
  const char *q = p ? strchr(p, ']') : NULL;
  if (!p || !q || q >= end) {
    return -1;
  }
  size_t i = 0;
  while (p < q && i + 1 < cap) {
    if (*p == '"') {
      p++;
      if (i && i + 2 < cap) {
        out[i++] = ',';
        out[i++] = ' ';
      }
      while (p < q && *p != '"' && i + 1 < cap) {
        out[i++] = *p++;
      }
    }
    p++;
  }
  out[i] = '\0';
  return out[0] ? 0 : -1;
}

static int parse_catalog(const char *json) {
  game_count = 0;
  if (!json || !games) {
    return 0;
  }
  const char *p = strstr(json, "\"games\"");
  if (p)
    p = strchr(p, '[');
  if (!p)
    p = json;

  while ((p = strchr(p, '{')) != NULL && game_count < STORE_MAX_GAMES) {
    const char *end = p;
    int depth = 0;
    while (*end) {
      if (*end == '{')
        depth++;
      else if (*end == '}') {
        depth--;
        if (depth == 0)
          break;
      }
      end++;
    }
    if (!*end) {
      break;
    }
    store_game_t *g = &games[game_count];
    memset(g, 0, sizeof(*g));
    if (json_string_after(p, end, "id", g->id, sizeof(g->id)) == 0 &&
        json_string_after(p, end, "title", g->title, sizeof(g->title)) == 0) {
      json_string_after(p, end, "version", g->version, sizeof(g->version));
      json_string_after(p, end, "summary", g->summary, sizeof(g->summary));
      json_arches_after(p, end, g->arch, sizeof(g->arch));
      json_array_strings_after(p, end, "tags", g->tags, sizeof(g->tags));
      game_count++;
    }
    p = end + 1;
  }
  return game_count;
}

static int contains_casefold(const char *text, const char *needle) {
  if (!needle || !needle[0]) {
    return 1;
  }
  if (!text) {
    return 0;
  }
  for (const char *p = text; *p; ++p) {
    const char *a = p;
    const char *b = needle;
    while (*a && *b &&
           tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
      a++;
      b++;
    }
    if (!*b) {
      return 1;
    }
  }
  return 0;
}

static int filter_catalog(const char *query) {
    if (!catalog_body || !games) {
        return 0;
    }
    store_game_t *all_games = heap_caps_malloc(
        STORE_MAX_GAMES * sizeof(store_game_t),
        MALLOC_CAP_8BIT);
    if (!all_games) {
        game_count = 0;
        return 0;
    }
    int all_count = parse_catalog(catalog_body);
    memcpy(all_games, games, STORE_MAX_GAMES * sizeof(store_game_t));
    if (!query || !query[0]) {
        heap_caps_free(all_games);
        return all_count;
    }
    game_count = 0;
    for (int i = 0; i < all_count && game_count < STORE_MAX_GAMES; ++i) {
        if (contains_casefold(all_games[i].title, query) ||
            contains_casefold(all_games[i].tags, query) ||
            contains_casefold(all_games[i].id, query)) {
            games[game_count++] = all_games[i];
        }
    }
    heap_caps_free(all_games);
    return game_count;
}

static int fetch_catalog(const char *base_url, char *status,
                         size_t status_len) {
  char url[PRG32_STORE_URL_MAX_LEN + 16];
  snprintf(url, sizeof(url), "%s/api/games", base_url);
  ESP_LOGI(TAG, "fetch catalog: %s heap=%lu", url,
           (unsigned long)esp_get_free_heap_size());
  if (!catalog_body || !games) {
    snprintf(status, status_len, "NO MEM");
    return -1;
  }
  memset(catalog_body, 0, PRG32_STORE_CATALOG_MAX_BYTES);
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = PRG32_STORE_HTTP_TIMEOUT_MS,
      .keep_alive_enable = false,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGI(TAG, "catalog client init failed heap=%lu",
             (unsigned long)esp_get_free_heap_size());
    snprintf(status, status_len, "NO MEM");
    return -1;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "catalog open failed: %s", esp_err_to_name(err));
    snprintf(status, status_len, "%s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return -1;
  }
  int content_len = esp_http_client_fetch_headers(client);
  int http_status = esp_http_client_get_status_code(client);
  if (http_status < 200 || http_status >= 300) {
    ESP_LOGI(TAG, "catalog fetch HTTP status=%d", http_status);
    snprintf(status, status_len, "%d", http_status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  bool truncated = false;
  size_t len = 0;
  while (len + 1 < PRG32_STORE_CATALOG_MAX_BYTES) {
    int got = esp_http_client_read(client, catalog_body + len,
                                   PRG32_STORE_CATALOG_MAX_BYTES - len - 1);
    if (got < 0) {
      ESP_LOGI(TAG, "catalog read failed");
      snprintf(status, status_len, "READ");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return -1;
    }
    if (got == 0) {
      break;
    }
    len += (size_t)got;
    catalog_body[len] = '\0';
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (content_len > 0 && (size_t)content_len > len) {
    truncated = true;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  parse_catalog(catalog_body);
  ESP_LOGI(TAG, "catalog fetch ok: status=%d content_len=%d bytes=%lu games=%d",
           http_status, content_len, (unsigned long)len, game_count);
  snprintf(status, status_len, truncated ? "first %d shown" : "OK",
           STORE_MAX_GAMES);
  return 0;
}

static const char *current_arch(void) {
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
  return PRG32_CART_ARCH_QEMU;
#else
  return PRG32_CART_ARCH_ESP32C6;
#endif
}

static int game_is_compatible(const store_game_t *game) {
  return game && strstr(game->arch, current_arch()) != NULL;
}

static int validate_download_header(const uint8_t *data, size_t len, char *status,
                                    size_t status_len) {
  if (!data || len < sizeof(prg32_cart_header_t)) {
    snprintf(status, status_len, "SHORT HEADER");
    return -1;
  }
  const prg32_cart_header_t *h = (const prg32_cart_header_t *)data;
  if (memcmp(h->magic, PRG32_CART_MAGIC, sizeof(h->magic)) != 0) {
    snprintf(status, status_len, "BAD MAGIC");
    return -1;
  }
  if (h->abi_major != PRG32_CART_ABI_MAJOR) {
    snprintf(status, status_len, "ABI MAJOR %u!=%u", (unsigned)h->abi_major,
             (unsigned)PRG32_CART_ABI_MAJOR);
    return -1;
  }
  uint32_t import_model = PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE;
  if (h->header_size >= sizeof(prg32_cart_header_v2_t)) {
    if (len < sizeof(prg32_cart_header_v2_t)) {
      snprintf(status, status_len, "SHORT ABI");
      return -1;
    }
    const prg32_cart_header_v2_t *v2 = (const prg32_cart_header_v2_t *)data;
    import_model = v2->import_model;
    if (import_model == PRG32_IMPORT_MODEL_ABI_TABLE) {
      if (v2->abi_hash != PRG32_ABI_HASH) {
        snprintf(status, status_len, "ABI HASH");
        return -1;
      }
      uint32_t missing = v2->required_features & ~prg32_abi_table.provided_features;
      if (missing != 0u) {
        snprintf(status, status_len, "ABI FEAT 0x%lx",
                 (unsigned long)missing);
        return -1;
      }
      if ((h->flags & PRG32_CART_FLAG_ABI_TABLE) == 0u) {
        snprintf(status, status_len, "ABI FLAG");
        return -1;
      }
      return 0;
    }
  }
  if (import_model == PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE &&
      h->load_addr != PRG32_CART_LOAD_ADDR) {
    snprintf(status, status_len, "LEGACY REBUILD");
    return -1;
  }
  if (import_model != PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE) {
    snprintf(status, status_len, "IMPORT MODEL");
    return -1;
  }
  return 0;
}

static int stream_download(const char *base_url, const store_game_t *game,
                           uint8_t slot, char *status, size_t status_len) {
  char url[256];
  snprintf(url, sizeof(url),
           "%s/api/games/%s/download?architecture=%s&version=%s", base_url,
           game->id, current_arch(),
           game->version[0] ? game->version : "latest");
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = PRG32_STORE_HTTP_TIMEOUT_MS,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    snprintf(status, status_len, "CLIENT");
    return -1;
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    esp_http_client_cleanup(client);
    snprintf(status, status_len, "TIMEOUT");
    return -1;
  }
  int content_len = esp_http_client_fetch_headers(client);
  int http_status = esp_http_client_get_status_code(client);
  size_t slot_size = prg32_cart_slot_size(slot);
  if (http_status < 200 || http_status >= 300) {
    snprintf(status, status_len, "%d", http_status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  if (content_len <= 0 || (size_t)content_len > PRG32_CART_MAX_SIZE ||
      (size_t)content_len > slot_size) {
    snprintf(status, status_len, "TOO LARGE");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  uint8_t *chunk = heap_caps_malloc(PRG32_STORE_CHUNK_BYTES, MALLOC_CAP_8BIT);
  if (!chunk) {
    snprintf(status, status_len, "NO MEM");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  size_t offset = 0;
  size_t header_len = 0;
  while (header_len < sizeof(prg32_cart_header_v2_t) &&
         header_len < (size_t)content_len) {
    int read = esp_http_client_read(client, (char *)chunk + header_len,
                                    PRG32_STORE_CHUNK_BYTES - header_len);
    if (read <= 0) {
      snprintf(status, status_len, "TIMEOUT");
      heap_caps_free(chunk);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return -1;
    }
    header_len += (size_t)read;
  }
  if (validate_download_header(chunk, header_len, status, status_len) != 0) {
    heap_caps_free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  if (prg32_cart_stream_begin(slot, (size_t)content_len) != 0) {
    snprintf(status, status_len, "%s", prg32_cart_last_error());
    heap_caps_free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  if (prg32_cart_stream_write(slot, 0, chunk, header_len) != 0) {
    snprintf(status, status_len, "%s", prg32_cart_last_error());
    heap_caps_free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -1;
  }
  offset = header_len;
  while (offset < (size_t)content_len) {
    int read = esp_http_client_read(client, (char *)chunk, PRG32_STORE_CHUNK_BYTES);
    if (read <= 0) {
      snprintf(status, status_len, "TIMEOUT");
      heap_caps_free(chunk);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return -1;
    }
    if (prg32_cart_stream_write(slot, offset, chunk, (size_t)read) != 0) {
      snprintf(status, status_len, "%s", prg32_cart_last_error());
      heap_caps_free(chunk);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return -1;
    }
    offset += (size_t)read;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  heap_caps_free(chunk);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (prg32_cart_stream_end(slot, offset) != 0) {
    snprintf(status, status_len, "%s", prg32_cart_last_error());
    return -1;
  }
  snprintf(status, status_len, "OK");
  ESP_LOGI(TAG, "downloaded %s to cart%u", game->id, (unsigned)slot);
  return 0;
}

static void draw_store_list(int selected, int page, const char *note) {
  int pages =
      game_count > 0 ? (game_count + STORE_PAGE_SIZE - 1) / STORE_PAGE_SIZE : 1;
  char line[48];
  prg32_gfx_clear(PRG32_COLOR_BLACK);
  snprintf(line, sizeof(line), "BROWSE STORE  [page %d/%d]", page + 1, pages);
  prg32_gfx_text8(8, 8, line, PRG32_COLOR_WHITE, 0);
  prg32_gfx_text8(8, 26, "SEARCH",
                  selected == -1 ? PRG32_COLOR_GREEN : PRG32_COLOR_CYAN, 0);
  int start = page * STORE_PAGE_SIZE;
  for (int i = 0; i < STORE_PAGE_SIZE && start + i < game_count; ++i) {
    const store_game_t *g = &games[start + i];
    int idx = start + i;
    snprintf(line, sizeof(line), "%c %-24s %.8s", idx == selected ? '>' : ' ',
             g->title, g->version);
    prg32_gfx_text8(
        8, 46 + i * 18, line,
        game_is_compatible(g) ? PRG32_COLOR_WHITE : PRG32_COLOR_YELLOW, 0);
  }
  if (note && note[0]) {
    prg32_gfx_text8(8, 190, note, PRG32_COLOR_YELLOW, 0);
  }
  prg32_gfx_text8(8, 216, "U/D SCROLL L/R PAGE SELECT DETAILS A BACK",
                  PRG32_COLOR_CYAN, 0);
  prg32_gfx_present();
}

static int run_detail(const char *base_url, int index) {
  uint8_t slot = 0;
  const store_game_t *g = &games[index];
  prg32_input_wait_released(STORE_ACCEPT | STORE_CANCEL);
  while (1) {
    char line[48];
    uint16_t action_color =
        game_is_compatible(g) ? PRG32_COLOR_CYAN : PRG32_COLOR_YELLOW;
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    snprintf(line, sizeof(line), "%.24s  v%.10s", g->title, g->version);
    prg32_gfx_text8(8, 8, line, PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 36, g->summary, PRG32_COLOR_CYAN, 0);
    snprintf(line, sizeof(line), "Architectures: %s",
             game_is_compatible(g) ? current_arch() : "(none)");
    prg32_gfx_text8(8, 96, line, action_color, 0);
    snprintf(line, sizeof(line), "Slot: cart%u", (unsigned)slot);
    prg32_gfx_text8(8, 126, line, PRG32_COLOR_WHITE, 0);
    if (!game_is_compatible(g)) {
      prg32_gfx_text8(8, 154, "NOT COMPATIBLE WITH THIS FIRMWARE",
                      PRG32_COLOR_YELLOW, 0);
    }
    prg32_gfx_text8(8, 216, "U/D SLOT SELECT DOWNLOAD  A BACK", action_color,
                    0);
    prg32_gfx_present();
    uint32_t input = prg32_input_read_menu();
    if (input & PRG32_BTN_UP) {
      slot = (slot + PRG32_CART_SLOT_COUNT - 1) % PRG32_CART_SLOT_COUNT;
      prg32_input_wait_released(PRG32_BTN_UP);
    } else if (input & PRG32_BTN_DOWN) {
      slot = (slot + 1) % PRG32_CART_SLOT_COUNT;
      prg32_input_wait_released(PRG32_BTN_DOWN);
    } else if (input & STORE_CANCEL) {
      prg32_input_wait_released(STORE_CANCEL);
      return 0;
    } else if ((input & STORE_ACCEPT) && game_is_compatible(g)) {
      char status[48];
      wait_and_show("DOWNLOADING...", 10);
      if (stream_download(base_url, g, slot, status, sizeof(status)) == 0) {
        wait_and_show("INSTALLED", 2000);
        while (1) {
          prg32_gfx_clear(PRG32_COLOR_BLACK);
          prg32_gfx_text8(8, 8, "INSTALLED", PRG32_COLOR_GREEN, 0);
          prg32_gfx_text8(8, 216, "SELECT RUN NOW  A BACK", PRG32_COLOR_CYAN,
                          0);
          prg32_gfx_present();
          uint32_t run_input = prg32_input_read_menu();
          if (run_input & PRG32_BTN_SELECT) {
            prg32_cart_select_slot(slot);
            prg32_input_wait_released(PRG32_BTN_SELECT);
            return 1;
          }
          if (run_input & STORE_CANCEL) {
            prg32_input_wait_released(STORE_CANCEL);
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "FAILED: %s", status);
        wait_and_show(msg, 3000);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void prg32_setup_store_run(void) {
    int choice = 0;
    static const char *const items[] = {
        "AUTO-DISCOVER",
        "MANUAL ENTRY",
        "CLEAR SAVED URL",
        "BACK",
    };
    prg32_input_wait_released(STORE_ACCEPT | STORE_CANCEL);
    while (1) {
        char current[PRG32_STORE_URL_MAX_LEN];
        int has_current = prg32_store_url_resolve(current, sizeof(current)) == 0;
        uint32_t last = prg32_input_read_menu();
        while (1) {
            uint32_t input = prg32_input_read_menu();
            if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
                choice--;
            }
            if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN) && choice < 3) {
                choice++;
            }
            if ((input & STORE_CANCEL) && !(last & STORE_CANCEL)) {
                prg32_input_wait_released(STORE_CANCEL);
                return;
            }
            if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
                ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
                prg32_input_wait_released(STORE_ACCEPT);
                if (choice == 0) {
                    char found[PRG32_STORE_URL_MAX_LEN];
                    wait_and_show("SCANNING...", 10);
                    if (prg32_store_discover(found, sizeof(found)) == 0) {
                        while (1) {
                            uint32_t save_input = prg32_input_read_menu();
                            prg32_gfx_clear(PRG32_COLOR_BLACK);
                            prg32_gfx_text8(8, 8, "CARTRIDGE STORE", PRG32_COLOR_WHITE, 0);
                            prg32_gfx_text8(8, 40, "FOUND:", PRG32_COLOR_GREEN, 0);
                            prg32_gfx_text8(8, 58, found, PRG32_COLOR_WHITE, 0);
                            prg32_gfx_text8(8, 216, "SELECT SAVE  A DISCARD", PRG32_COLOR_CYAN, 0);
                            prg32_gfx_present();
                            if (save_input & PRG32_BTN_SELECT) {
                                prg32_store_url_set(found);
                                prg32_input_wait_released(STORE_ACCEPT);
                                wait_and_show("SAVED", 1000);
                                break;
                            }
                            if (save_input & STORE_CANCEL) {
                                prg32_input_wait_released(STORE_CANCEL);
                                break;
                            }
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                    } else {
                        wait_and_show("NOT FOUND", 2000);
                    }
                    break;
                }
                if (choice == 1) {
                    char url[PRG32_STORE_URL_MAX_LEN] = "";
                    if (prg32_text_input(url, sizeof(url), "STORE URL") >= 0 && url[0]) {
                        normalize_store_url(url, sizeof(url));
                        if (prg32_store_url_set(url) == 0) {
                            wait_and_show("SAVED", 1000);
                        } else {
                            wait_and_show("SAVE FAILED", 1000);
                        }
                    }
                    break;
                }
                if (choice == 2) {
                    prg32_store_url_clear();
                    wait_and_show("CLEARED", 1000);
                    break;
                }
                return;
            }
            prg32_gfx_clear(PRG32_COLOR_BLACK);
            prg32_gfx_text8(8, 8, "CARTRIDGE STORE", PRG32_COLOR_WHITE, 0);
            for (int i = 0; i < 4; ++i) {
                int y = 42 + i * 18;
                prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
                prg32_gfx_text8(24, y, items[i], PRG32_COLOR_WHITE, 0);
            }
            prg32_gfx_text8(8, 144, "CURRENT:", PRG32_COLOR_YELLOW, 0);
            prg32_gfx_text8(8, 162, has_current ? current : "(not configured)", PRG32_COLOR_CYAN, 0);
            prg32_gfx_text8(8, 216, "UP/DOWN MOVE  SELECT/B OK  A BACK", PRG32_COLOR_CYAN, 0);
            prg32_gfx_present();
            last = input;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void prg32_setup_store_browse_run(void) {
  prg32_wifi_scores_init();
  prg32_scores_api_start();
  char url[PRG32_STORE_URL_MAX_LEN];
  if (prg32_store_url_resolve(url, sizeof(url)) != 0) {
#ifdef PRG32_STORE_SERVER_URL
    snprintf(url, sizeof(url), "%s", PRG32_STORE_SERVER_URL);
    prg32_store_url_set(url);
#else
    wait_and_show("CONFIGURE STORE FIRST", 2000);
    return;
#endif
  }
  char status[32];
  if (store_buffers_alloc(status, sizeof(status)) != 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "UNAVAILABLE: %s", status);
    wait_and_show(msg, 2000);
    return;
  }
  wait_and_show("CONNECTING...", 10);
  if (fetch_catalog(url, status, sizeof(status)) != 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "UNAVAILABLE: %s", status);
    wait_and_show(msg, 2000);
    goto done;
  }
  if (game_count == 0) {
    wait_and_show("NO GAMES", 2000);
    goto done;
  }
  int selected = 0;
  int page = 0;
  const char *note = strcmp(status, "OK") == 0 ? "" : status;
  prg32_input_wait_released(STORE_ACCEPT | STORE_CANCEL);
  while (1) {
    draw_store_list(selected, page, note);
    uint32_t input = prg32_input_read_menu();
    if (input & STORE_CANCEL) {
      prg32_input_wait_released(STORE_CANCEL);
      goto done;
    }
    if (input & PRG32_BTN_UP) {
      if (selected > 0) {
        selected--;
        page = selected / STORE_PAGE_SIZE;
      } else if (selected == 0) {
        selected = -1;
      }
      prg32_input_wait_released(PRG32_BTN_UP);
    } else if (input & PRG32_BTN_DOWN) {
      if (selected < 0 && game_count > 0) {
        selected = 0;
        page = 0;
      } else if (selected + 1 < game_count) {
        selected++;
        page = selected / STORE_PAGE_SIZE;
      }
      prg32_input_wait_released(PRG32_BTN_DOWN);
    } else if (input & PRG32_BTN_LEFT) {
      if (page > 0) {
        page--;
        selected = page * STORE_PAGE_SIZE;
      }
      prg32_input_wait_released(PRG32_BTN_LEFT);
    } else if (input & PRG32_BTN_RIGHT) {
      int pages = (game_count + STORE_PAGE_SIZE - 1) / STORE_PAGE_SIZE;
      if (page + 1 < pages) {
        page++;
        selected = page * STORE_PAGE_SIZE;
      }
      prg32_input_wait_released(PRG32_BTN_RIGHT);
    } else if (input & STORE_ACCEPT) {
      if (selected < 0) {
        char query[40] = "";
        if (prg32_text_input(query, sizeof(query), "SEARCH STORE") >= 0) {
          filter_catalog(query);
          selected = game_count > 0 ? 0 : -1;
          page = 0;
          note = query[0] ? "filtered" : "";
        }
        prg32_input_wait_released(STORE_ACCEPT);
      } else if (run_detail(url, selected)) {
        goto done;
      } else {
        prg32_input_wait_released(STORE_ACCEPT);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

done:
  store_buffers_free();
}
