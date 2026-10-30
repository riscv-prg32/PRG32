#include "prg32.h"
#include "prg32_config.h"

#include "cJSON.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_IDF_TARGET_ESP32C6
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "driver/uart_vfs.h"
#endif

#define PROTOCOL_VERSION 1u
#define PROTOCOL_MAX_PAYLOAD (16u * 1024u)
#define PROTOCOL_CHUNK_MAX 4096u
/* Measured worst case on 2026-10-29 (uploads, runs, and streaming a 5 KB
 * performance result): 2,712 bytes. The rest is margin. */
#define PROTOCOL_TASK_STACK 4608u
/* The USB Serial/JTAG driver drops received bytes that do not fit in its
 * ring, so the receive ring stays close to one 4,124-byte UPLOAD_DATA frame
 * while the protocol task drains it. */
#define PROTOCOL_RX_RING_BYTES 4096u
/* Console logs share the wire with protocol frames. Every reply frame is
 * written to the transmit ring in one call so log text can only land between
 * frames, never inside one; a frame must therefore fit in the ring. */
#define PROTOCOL_TX_RING_BYTES 1024u
#define PROTOCOL_TX_FRAME_MAX PROTOCOL_TX_RING_BYTES
/* RESULT_DATA chunk size: one header plus this payload fits in a frame. */
#define PROTOCOL_RESULT_CHUNK 768u
/* A frame that stops arriving for this long is abandoned so the decoder can
 * search for the next magic instead of consuming unrelated bytes. */
#define PROTOCOL_BYTE_TIMEOUT_MS 1000u
/* An upload that receives no frame for this long is cancelled, its partly
 * written slot is erased, and the device returns to setup. */
#define PROTOCOL_UPLOAD_IDLE_MS 10000u
#define PROTOCOL_RESULT_HOLD_MS 1200u
/* A full-screen present takes about 150 ms on the ILI9341, so progress is
 * redrawn at most this often while chunks keep arriving. */
#define PROTOCOL_PROGRESS_REDRAW_MS 200u

/* Joining a network waits this long for an address before the previous
 * Wi-Fi configuration is restored. */
#define PROTOCOL_WIFI_JOIN_TIMEOUT_MS 20000u

#if PRG32_WIFI_ENABLE
#define PROTOCOL_WIFI_CAPABILITY ",\"wifi\""
#define PROTOCOL_HAS_WIFI 1
#else
#define PROTOCOL_WIFI_CAPABILITY ""
#define PROTOCOL_HAS_WIFI 0
#endif

/* KEY: a press lasts this long unless the request sets hold_ms. A full-screen
 * present takes about 150 ms, so the default outlasts one slow frame. The
 * maximum releases a key whose host disappeared without sending a release. */
#define PROTOCOL_KEY_HOLD_DEFAULT_MS 200u
#define PROTOCOL_KEY_HOLD_MAX_MS 60000u

/* Header flag bit 0: final chunk on responses, restart on PERFORMANCE_GET. */
#define PROTOCOL_FLAG_FINAL 0x0001u
#define PROTOCOL_FLAG_RESTART 0x0001u

enum {
  MESSAGE_HELLO = 0x01,
  MESSAGE_UPLOAD_BEGIN = 0x02,
  MESSAGE_UPLOAD_DATA = 0x03,
  MESSAGE_UPLOAD_END = 0x04,
  MESSAGE_RUN = 0x05,
  MESSAGE_DEBUG = 0x06,
  MESSAGE_PERFORMANCE_GET = 0x07,
  MESSAGE_WIFI_SCAN = 0x08,
  MESSAGE_WIFI_SET = 0x09,
  MESSAGE_WIFI_FORGET = 0x0A,
  MESSAGE_KEY = 0x0B,
  MESSAGE_STATUS = 0x80,
  MESSAGE_RESULT_DATA = 0x81,
};

typedef struct __attribute__((packed)) {
  uint8_t magic[8];
  uint8_t version;
  uint8_t type;
  uint16_t flags;
  uint32_t request_id;
  uint32_t payload_size;
  uint32_t payload_crc32;
} protocol_header_t;

typedef struct {
  bool active;
  uint8_t slot;
  size_t expected_size;
  size_t received_size;
  uint32_t expected_crc32;
  uint32_t running_crc32;
  uint32_t last_frame_ms;
  uint32_t last_draw_ms;
  char name[33];
  char command[16];
} upload_state_t;

static const char *TAG = "prg32_usb";
static const uint8_t PROTOCOL_MAGIC[8] = {'P', 'R', 'G', '3', '2', 'I', 'O', 0};
static upload_state_t g_upload;
/* Read by the main loop and setup menu while the protocol task writes it. */
static volatile bool g_upload_busy;
static bool g_transport_ready;
static char *g_performance_json;
static size_t g_performance_offset;

bool prg32_usb_protocol_busy(void) { return g_upload_busy; }

#if !CONFIG_IDF_TARGET_ESP32C6
void prg32_qemu_input_feed(uint8_t byte);
#endif

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
  crc = ~crc;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

static int transport_read(void *data, size_t size, TickType_t wait) {
#if CONFIG_IDF_TARGET_ESP32C6
  return usb_serial_jtag_read_bytes(data, (uint32_t)size, wait);
#else
  return uart_read_bytes(UART_NUM_0, data, (uint32_t)size, wait);
#endif
}

/* Write one whole frame. The USB Serial/JTAG driver copies it into the
 * transmit ring in one step; the UART driver holds its transmit lock for the
 * whole call. Either way no console text can be inserted inside the frame. */
static bool transport_write_frame(const void *data, size_t size) {
#if CONFIG_IDF_TARGET_ESP32C6
  return usb_serial_jtag_write_bytes(data, (uint32_t)size,
                                     pdMS_TO_TICKS(2000)) == (int)size;
#else
  return uart_write_bytes(UART_NUM_0, data, size) == (int)size;
#endif
}

/* Read exactly size bytes of one frame. A silent gap longer than
 * PROTOCOL_BYTE_TIMEOUT_MS abandons the frame. */
static bool read_exact(void *data, size_t size) {
  uint8_t *output = data;
  size_t received = 0;
  while (received < size) {
    int count = transport_read(output + received, size - received,
                               pdMS_TO_TICKS(PROTOCOL_BYTE_TIMEOUT_MS));
    if (count <= 0) {
      return false;
    }
    received += (size_t)count;
  }
  return true;
}

static void send_frame(uint8_t type, uint16_t flags, uint32_t request_id,
                       const void *payload, size_t payload_size) {
  size_t frame_size = sizeof(protocol_header_t) + payload_size;
  if (frame_size > PROTOCOL_TX_FRAME_MAX) {
    ESP_LOGE(TAG, "reply frame of %u bytes exceeds %u", (unsigned)frame_size,
             (unsigned)PROTOCOL_TX_FRAME_MAX);
    return;
  }
  uint8_t *frame = malloc(frame_size);
  if (!frame) {
    return;
  }
  protocol_header_t header = {
      .version = PROTOCOL_VERSION,
      .type = type,
      .flags = flags,
      .request_id = request_id,
      .payload_size = (uint32_t)payload_size,
      .payload_crc32 = crc32_update(0, payload, payload_size),
  };
  memcpy(header.magic, PROTOCOL_MAGIC, sizeof(header.magic));
  memcpy(frame, &header, sizeof(header));
  if (payload_size > 0) {
    memcpy(frame + sizeof(header), payload, payload_size);
  }
  transport_write_frame(frame, frame_size);
  free(frame);
}

static void send_status(uint32_t request_id, bool ok, const char *message) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return;
  }
  cJSON_AddBoolToObject(root, "ok", ok);
  cJSON_AddStringToObject(root, ok ? "message" : "error",
                         message ? message : "");
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json) {
    send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, json,
               strlen(json));
    cJSON_free(json);
  }
}

static int slot_from_name(const char *name) {
  if (!name || strncmp(name, "cart", 4) != 0 || name[4] < '0' ||
      name[4] > '7' || name[5] != 0) {
    return -1;
  }
  return name[4] - '0';
}

static bool command_is_known(const char *command) {
  return strcmp(command, "store") == 0 || strcmp(command, "run") == 0 ||
         strcmp(command, "debug") == 0 ||
         strcmp(command, "performance") == 0;
}

static unsigned upload_percent(void) {
  if (g_upload.expected_size == 0) {
    return 0;
  }
  unsigned percent =
      (unsigned)(g_upload.received_size * 100u / g_upload.expected_size);
  return percent > 100 ? 100 : percent;
}

static void draw_upload_screen(const char *status, uint16_t color) {
  char slot_line[48];
  char progress_line[48];
  unsigned progress = upload_percent();
  snprintf(slot_line, sizeof(slot_line), "TARGET: cart%u", g_upload.slot);
  snprintf(progress_line, sizeof(progress_line), "%u%%  %lu / %lu bytes",
           progress, (unsigned long)g_upload.received_size,
           (unsigned long)g_upload.expected_size);

  prg32_gfx_lock();
  prg32_gfx_set_fullscreen(1);
  prg32_gfx_clear(PRG32_COLOR_BLACK);
  prg32_gfx_text8(8, 12, "USB CARTRIDGE UPLOAD", PRG32_COLOR_WHITE, 0);
  prg32_gfx_text8(8, 42, g_upload.name, PRG32_COLOR_CYAN, 0);
  prg32_gfx_text8(8, 62, slot_line, PRG32_COLOR_YELLOW, 0);
  prg32_gfx_rect(16, 104, 288, 20, PRG32_COLOR_WHITE);
  if (progress > 0) {
    prg32_gfx_rect(19, 107, (282 * (int)progress) / 100, 14,
                   PRG32_COLOR_GREEN);
  }
  prg32_gfx_text8(8, 138, progress_line, PRG32_COLOR_WHITE, 0);
  prg32_gfx_text8(8, 174, status ? status : "UPLOADING...", color, 0);
  prg32_gfx_present();
  prg32_gfx_unlock();
  g_upload.last_draw_ms = prg32_ticks_ms();
}

/* Leave the upload screen and hand control back to the main loop. */
static void upload_release(bool open_setup) {
  g_upload.active = false;
  g_upload_busy = false;
  if (open_setup) {
    prg32_setup_request();
  }
}

/* Show a failure, erase the incomplete slot so setup never lists a partial
 * image, and return to setup. Used for every failure after the erase began. */
static void upload_fail(uint32_t request_id, const char *screen_text,
                        const char *error) {
  draw_upload_screen(screen_text, PRG32_COLOR_RED);
  if (request_id != 0) {
    send_status(request_id, false, error);
  }
  ESP_LOGW(TAG, "upload to cart%u failed: %s", g_upload.slot, error);
  prg32_cart_erase_slot(g_upload.slot);
  vTaskDelay(pdMS_TO_TICKS(PROTOCOL_RESULT_HOLD_MS));
  upload_release(true);
}

static const char *json_string(cJSON *root, const char *name) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool json_u32(cJSON *root, const char *name, uint32_t *value) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
      item->valuedouble > 4294967295.0) {
    return false;
  }
  *value = (uint32_t)item->valuedouble;
  return true;
}

static void handle_hello(uint32_t request_id) {
  char json[640];
  char keys[96] = "";
  for (int i = 0; i < PRG32_REMOTE_KEY_COUNT; ++i) {
    size_t used = strlen(keys);
    snprintf(keys + used, sizeof(keys) - used, "%s\"%s\"", i ? "," : "",
             prg32_controller_remote_name(i));
  }
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"protocol\":\"prg32-io-1.0\","
           "\"capabilities\":[\"upload\",\"run\",\"performance\",\"keys\""
           PROTOCOL_WIFI_CAPABILITY "],\"keys\":[%s],"
           "\"debug_supported\":false,\"firmware\":\"%s\",\"target\":\"%s\","
           "\"slots\":%u,\"max_chunk\":%u,\"max_payload\":%u,"
           "\"runtime\":{\"cart_ram_size\":%lu,\"cart_max_size\":%lu,"
           "\"cart_abi_major\":%u,\"cart_abi_minor\":%u,"
           "\"heap_free\":%lu,\"heap_largest\":%lu}}",
           keys, PRG32_FIRMWARE_VERSION, CONFIG_IDF_TARGET,
           PRG32_CART_SLOT_COUNT,
           PROTOCOL_CHUNK_MAX, PROTOCOL_MAX_PAYLOAD,
           (unsigned long)prg32_cart_ram_size(),
           (unsigned long)PRG32_CART_MAX_SIZE, PRG32_CART_ABI_MAJOR,
           PRG32_CART_ABI_MINOR,
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, json,
             strlen(json));
}

static void handle_upload_begin(uint32_t request_id, const uint8_t *payload,
                                size_t payload_size) {
  cJSON *root = cJSON_ParseWithLength((const char *)payload, payload_size);
  uint32_t image_size = 0;
  uint32_t image_crc32 = 0;
  const char *name = root ? json_string(root, "name") : NULL;
  const char *slot_name = root ? json_string(root, "slot") : NULL;
  const char *command = root ? json_string(root, "command") : NULL;
  int slot = slot_from_name(slot_name);
  if (!command) {
    command = "store";
  }
  if (!root || !name || !json_u32(root, "size", &image_size) ||
      !json_u32(root, "crc32", &image_crc32) || slot < 0 ||
      image_size == 0 || image_size > PRG32_CART_MAX_SIZE ||
      !command_is_known(command)) {
    cJSON_Delete(root);
    send_status(request_id, false, "invalid upload metadata");
    return;
  }
  /* Refuse before erasing anything: instruction debugging belongs to
   * PRG32-Qt, and hardware debugging uses USB Serial/JTAG with OpenOCD. */
  if (strcmp(command, "debug") == 0) {
    cJSON_Delete(root);
    send_status(request_id, false,
                "debug execution is available in PRG32-Qt only");
    return;
  }

  memset(&g_upload, 0, sizeof(g_upload));
  g_upload.active = true;
  g_upload.slot = (uint8_t)slot;
  g_upload.expected_size = image_size;
  g_upload.expected_crc32 = image_crc32;
  g_upload.last_frame_ms = prg32_ticks_ms();
  snprintf(g_upload.name, sizeof(g_upload.name), "%.32s", name);
  snprintf(g_upload.command, sizeof(g_upload.command), "%.15s", command);
  cJSON_Delete(root);

  /* Pause the main loop and setup menu before the running cartridge is
   * unloaded; prg32_cart_unload() waits for an update/draw call to finish. */
  g_upload_busy = true;
  prg32_cart_unload();
  /* A held remote key must not reach whatever runs after the upload. */
  prg32_controller_remote_release(UINT32_MAX);
  draw_upload_screen("ERASING SLOT...", PRG32_COLOR_YELLOW);
  if (prg32_cart_stream_begin(g_upload.slot, g_upload.expected_size) != 0) {
    draw_upload_screen(prg32_cart_last_error(), PRG32_COLOR_RED);
    send_status(request_id, false, prg32_cart_last_error());
    vTaskDelay(pdMS_TO_TICKS(PROTOCOL_RESULT_HOLD_MS));
    upload_release(true);
    return;
  }
  draw_upload_screen("UPLOADING...", PRG32_COLOR_CYAN);
  g_upload.last_frame_ms = prg32_ticks_ms();
  send_status(request_id, true, "upload accepted");
}

static void handle_upload_data(uint32_t request_id, const uint8_t *payload,
                               size_t payload_size) {
  if (!g_upload.active) {
    send_status(request_id, false, "no upload in progress");
    return;
  }
  if (payload_size < sizeof(uint32_t) ||
      payload_size - sizeof(uint32_t) > PROTOCOL_CHUNK_MAX ||
      payload_size == sizeof(uint32_t)) {
    send_status(request_id, false, "invalid upload chunk");
    return;
  }
  uint32_t offset = 0;
  memcpy(&offset, payload, sizeof(offset));
  const uint8_t *data = payload + sizeof(offset);
  size_t data_size = payload_size - sizeof(offset);
  char response[128];
  if ((size_t)offset + data_size == g_upload.received_size) {
    /* A host that lost the previous reply resends the same chunk. It is
     * already in flash and covered by the final CRC, so acknowledge it. */
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"received\":%lu,\"total\":%lu}",
             (unsigned long)g_upload.received_size,
             (unsigned long)g_upload.expected_size);
    send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, response,
               strlen(response));
    return;
  }
  if (offset != g_upload.received_size ||
      data_size > g_upload.expected_size - g_upload.received_size) {
    send_status(request_id, false, "out-of-order upload chunk");
    return;
  }
  if (prg32_cart_stream_write(g_upload.slot, offset, data, data_size) != 0) {
    upload_fail(request_id, "FLASH WRITE FAILED", prg32_cart_last_error());
    return;
  }
  g_upload.running_crc32 =
      crc32_update(g_upload.running_crc32, data, data_size);
  g_upload.received_size += data_size;
  if (g_upload.received_size == g_upload.expected_size ||
      prg32_ticks_ms() - g_upload.last_draw_ms >= PROTOCOL_PROGRESS_REDRAW_MS) {
    draw_upload_screen("UPLOADING...", PRG32_COLOR_CYAN);
  }
  snprintf(response, sizeof(response),
           "{\"ok\":true,\"received\":%lu,\"total\":%lu}",
           (unsigned long)g_upload.received_size,
           (unsigned long)g_upload.expected_size);
  send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, response,
             strlen(response));
}

static void finish_upload(uint32_t request_id) {
  if (!g_upload.active) {
    send_status(request_id, false, "no upload in progress");
    return;
  }
  if (g_upload.received_size != g_upload.expected_size ||
      g_upload.running_crc32 != g_upload.expected_crc32) {
    upload_fail(request_id, "UPLOAD FAILED",
                "upload length or checksum mismatch");
    return;
  }
  if (prg32_cart_stream_end(g_upload.slot, g_upload.expected_size) != 0) {
    upload_fail(request_id, "INVALID CARTRIDGE", prg32_cart_last_error());
    return;
  }

  bool should_run = strcmp(g_upload.command, "run") == 0 ||
                    strcmp(g_upload.command, "performance") == 0;
  if (should_run && prg32_cart_select_slot(g_upload.slot) != 0) {
    /* The image is stored and verified; only starting it failed. Keep it so
     * the student can inspect it from setup. */
    draw_upload_screen("RUN FAILED", PRG32_COLOR_RED);
    send_status(request_id, false, prg32_cart_last_error());
    vTaskDelay(pdMS_TO_TICKS(PROTOCOL_RESULT_HOLD_MS));
    upload_release(true);
    return;
  }

  draw_upload_screen("UPLOAD SUCCESSFUL", PRG32_COLOR_GREEN);
  vTaskDelay(pdMS_TO_TICKS(PROTOCOL_RESULT_HOLD_MS));
  cJSON *response = cJSON_CreateObject();
  if (response) {
    char slot_name[8];
    snprintf(slot_name, sizeof(slot_name), "cart%u", g_upload.slot);
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "name", g_upload.name);
    cJSON_AddStringToObject(response, "slot", slot_name);
    cJSON_AddNumberToObject(response, "size", (double)g_upload.expected_size);
    cJSON_AddBoolToObject(response, "running", should_run);
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json) {
      send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, json,
                 strlen(json));
      cJSON_free(json);
    }
  }
  ESP_LOGI(TAG, "upload to cart%u complete (%s)", g_upload.slot,
           g_upload.command);
  upload_release(!should_run);
}

/* Called whenever no complete frame arrived: cancel an abandoned upload. */
static void check_upload_timeout(void) {
  if (g_upload.active &&
      prg32_ticks_ms() - g_upload.last_frame_ms > PROTOCOL_UPLOAD_IDLE_MS) {
    upload_fail(0, "UPLOAD TIMED OUT", "host stopped sending");
  }
}

static void handle_run(uint32_t request_id, const uint8_t *payload,
                       size_t payload_size, bool debug) {
  if (debug) {
    send_status(request_id, false,
                "debug execution is available in PRG32-Qt only");
    return;
  }
  if (g_upload.active) {
    send_status(request_id, false, "upload in progress");
    return;
  }
  cJSON *root = cJSON_ParseWithLength((const char *)payload, payload_size);
  int slot = root ? slot_from_name(json_string(root, "slot")) : -1;
  cJSON_Delete(root);
  /* A held remote key must not reach the newly selected cartridge. */
  prg32_controller_remote_release(UINT32_MAX);
  if (slot < 0 || prg32_cart_select_slot((uint8_t)slot) != 0) {
    send_status(request_id, false,
                slot < 0 ? "invalid cartridge slot" : prg32_cart_last_error());
    return;
  }
  send_status(request_id, true, "cartridge selected");
}

static void handle_performance_get(uint32_t request_id, uint16_t flags) {
  /* A restart request discards a transfer that an earlier host abandoned. */
  if ((flags & PROTOCOL_FLAG_RESTART) && g_performance_json) {
    prg32_performance_json_free(g_performance_json);
    g_performance_json = NULL;
    g_performance_offset = 0;
  }
  if (!g_performance_json) {
    g_performance_json = prg32_performance_json_alloc();
    g_performance_offset = 0;
  }
  if (!g_performance_json) {
    send_status(request_id, false, "performance results unavailable");
    return;
  }
  size_t total = strlen(g_performance_json);
  size_t remaining = total - g_performance_offset;
  size_t chunk = remaining < PROTOCOL_RESULT_CHUNK ? remaining
                                                   : PROTOCOL_RESULT_CHUNK;
  uint16_t response_flags = chunk == remaining ? PROTOCOL_FLAG_FINAL : 0;
  send_frame(MESSAGE_RESULT_DATA, response_flags, request_id,
             g_performance_json + g_performance_offset, chunk);
  g_performance_offset += chunk;
  if (g_performance_offset == total) {
    prg32_performance_json_free(g_performance_json);
    g_performance_json = NULL;
    g_performance_offset = 0;
  }
}

/* Add the stored network names (never passwords) to a reply. */
static void add_known_networks(cJSON *root) {
  char known[PRG32_WIFI_KNOWN_MAX][32];
  int count = prg32_wifi_known_networks(known, PRG32_WIFI_KNOWN_MAX);
  cJSON *list = cJSON_AddArrayToObject(root, "known");
  for (int i = 0; list && i < count; ++i) {
    cJSON_AddItemToArray(list, cJSON_CreateString(known[i]));
  }
}

static void add_connection(cJSON *root) {
  bool station = prg32_wifi_current_mode() == PRG32_WIFI_MODE_STA &&
                 strcmp(prg32_wifi_current_status(), "CONNECTED") == 0;
  cJSON_AddStringToObject(root, "connected",
                          station ? prg32_wifi_current_ssid() : "");
  cJSON_AddStringToObject(root, "ip", station ? prg32_wifi_current_ip() : "");
}

/* Send a reply object, removing the last network until the frame fits. */
static void send_wifi_reply(uint32_t request_id, cJSON *root) {
  cJSON *networks = cJSON_GetObjectItem(root, "networks");
  bool truncated = false;
  char *json = NULL;
  while (1) {
    cJSON_DeleteItemFromObject(root, "truncated");
    if (networks) {
      cJSON_AddBoolToObject(root, "truncated", truncated);
    }
    json = cJSON_PrintUnformatted(root);
    if (!json || strlen(json) + sizeof(protocol_header_t) <= PROTOCOL_TX_FRAME_MAX ||
        !networks || cJSON_GetArraySize(networks) == 0) {
      break;
    }
    cJSON_free(json);
    json = NULL;
    cJSON_DeleteItemFromArray(networks, cJSON_GetArraySize(networks) - 1);
    truncated = true;
  }
  if (json) {
    send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, json,
               strlen(json));
    cJSON_free(json);
  } else {
    send_status(request_id, false, "out of memory");
  }
}

static void handle_wifi_scan(uint32_t request_id) {
  prg32_wifi_network_t *networks =
      calloc(PRG32_WIFI_SCAN_MAX, sizeof(*networks));
  cJSON *root = cJSON_CreateObject();
  if (!networks || !root) {
    free(networks);
    cJSON_Delete(root);
    send_status(request_id, false, "out of memory");
    return;
  }
  char reason[48];
  int count = prg32_wifi_scan_networks(networks, PRG32_WIFI_SCAN_MAX, reason,
                                       sizeof(reason));
  if (count < 0) {
    free(networks);
    cJSON_Delete(root);
    send_status(request_id, false, reason);
    return;
  }
  char known[PRG32_WIFI_KNOWN_MAX][32];
  int known_count = prg32_wifi_known_networks(known, PRG32_WIFI_KNOWN_MAX);
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON *list = cJSON_AddArrayToObject(root, "networks");
  for (int i = 0; list && i < count; ++i) {
    cJSON *item = cJSON_CreateObject();
    if (!item) {
      break;
    }
    bool is_known = false;
    for (int k = 0; k < known_count; ++k) {
      is_known = is_known || strcmp(known[k], networks[i].ssid) == 0;
    }
    cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
    cJSON_AddNumberToObject(item, "rssi", networks[i].rssi);
    cJSON_AddNumberToObject(item, "channel", networks[i].channel);
    cJSON_AddStringToObject(item, "auth", networks[i].auth);
    cJSON_AddBoolToObject(item, "known", is_known);
    cJSON_AddItemToArray(list, item);
  }
  free(networks);
  add_known_networks(root);
  add_connection(root);
  send_wifi_reply(request_id, root);
  cJSON_Delete(root);
}

static void handle_wifi_set(uint32_t request_id, const uint8_t *payload,
                            size_t payload_size) {
  cJSON *request = cJSON_ParseWithLength((const char *)payload, payload_size);
  const char *ssid = request ? json_string(request, "ssid") : NULL;
  const char *password = request ? json_string(request, "password") : NULL;
  if (!ssid) {
    cJSON_Delete(request);
    send_status(request_id, false, "missing ssid");
    return;
  }
  char reason[48];
  int rc = prg32_wifi_connect_network(ssid, password ? password : "",
                                      PROTOCOL_WIFI_JOIN_TIMEOUT_MS, reason,
                                      sizeof(reason));
  /* The request held the password; clear it before releasing the memory. */
  if (password) {
    memset((char *)password, 0, strlen(password));
  }
  cJSON_Delete(request);
  if (rc != 0) {
    send_status(request_id, false, reason);
    return;
  }
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    send_status(request_id, true, "connected");
    return;
  }
  cJSON_AddBoolToObject(root, "ok", true);
  add_connection(root);
  add_known_networks(root);
  send_wifi_reply(request_id, root);
  cJSON_Delete(root);
}

static void handle_wifi_forget(uint32_t request_id, const uint8_t *payload,
                               size_t payload_size) {
  cJSON *request = cJSON_ParseWithLength((const char *)payload, payload_size);
  const char *ssid = request ? json_string(request, "ssid") : NULL;
  int rc = ssid ? prg32_wifi_forget_network(ssid) : -1;
  cJSON_Delete(request);
  if (!ssid) {
    send_status(request_id, false, "missing ssid");
    return;
  }
  if (rc != 0) {
    send_status(request_id, false, "network is not stored");
    return;
  }
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    send_status(request_id, true, "forgotten");
    return;
  }
  cJSON_AddBoolToObject(root, "ok", true);
  add_known_networks(root);
  add_connection(root);
  send_wifi_reply(request_id, root);
  cJSON_Delete(root);
}

/* Add every remote key that is currently down, by name, to a reply. */
static void add_active_keys(cJSON *root) {
  uint32_t active = prg32_controller_remote_active();
  cJSON *list = cJSON_AddArrayToObject(root, "active");
  for (int i = 0; list && i < PRG32_REMOTE_KEY_COUNT; ++i) {
    const char *name = prg32_controller_remote_name(i);
    if (active & prg32_controller_remote_bit(name)) {
      cJSON_AddItemToArray(list, cJSON_CreateString(name));
    }
  }
}

/* Convert the "keys" array to a button mask. An unknown name refuses the
 * whole request, so a typing mistake never presses half a chord. */
static bool key_mask_from_json(cJSON *keys, uint32_t *mask, char *reason,
                               size_t reason_size) {
  *mask = 0;
  cJSON *key = NULL;
  cJSON_ArrayForEach(key, keys) {
    uint32_t bit = cJSON_IsString(key)
                       ? prg32_controller_remote_bit(key->valuestring)
                       : 0;
    if (bit == 0) {
      snprintf(reason, reason_size, "unknown key %.16s",
               cJSON_IsString(key) ? key->valuestring : "(not a string)");
      return false;
    }
    *mask |= bit;
  }
  return true;
}

static void handle_key(uint32_t request_id, const uint8_t *payload,
                       size_t payload_size) {
  /* The main loop is paused during an upload, so nothing would read the key. */
  if (g_upload.active) {
    send_status(request_id, false, "upload in progress");
    return;
  }
  cJSON *request = cJSON_ParseWithLength((const char *)payload, payload_size);
  const char *action = request ? json_string(request, "action") : NULL;
  cJSON *keys = request ? cJSON_GetObjectItemCaseSensitive(request, "keys") : NULL;
  bool release = action && strcmp(action, "release") == 0;
  const char *error = NULL;
  char reason[32];
  uint32_t mask = 0;
  uint32_t hold_ms = PROTOCOL_KEY_HOLD_DEFAULT_MS;

  if (!request) {
    error = "invalid key request";
  } else if (action && !release && strcmp(action, "press") != 0) {
    error = "invalid key action";
  } else if (keys && !cJSON_IsArray(keys)) {
    error = "keys must be an array";
  } else if (keys && !key_mask_from_json(keys, &mask, reason, sizeof(reason))) {
    error = reason;
  } else if (!release && mask == 0) {
    error = "missing keys";
  } else if (cJSON_GetObjectItemCaseSensitive(request, "hold_ms") &&
             (!json_u32(request, "hold_ms", &hold_ms) || hold_ms == 0 ||
              hold_ms > PROTOCOL_KEY_HOLD_MAX_MS)) {
    error = "invalid hold_ms";
  }
  if (error) {
    send_status(request_id, false, error);
    cJSON_Delete(request);
    return;
  }
  cJSON_Delete(request);

  if (release) {
    /* A release that names no key releases all of them. */
    prg32_controller_remote_release(mask ? mask : UINT32_MAX);
  } else {
    prg32_controller_remote_press(mask, hold_ms);
  }

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    send_status(request_id, true, "key accepted");
    return;
  }
  cJSON_AddBoolToObject(root, "ok", true);
  add_active_keys(root);
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json) {
    send_frame(MESSAGE_STATUS, PROTOCOL_FLAG_FINAL, request_id, json,
               strlen(json));
    cJSON_free(json);
  } else {
    send_status(request_id, true, "key accepted");
  }
}

static void dispatch_frame(const protocol_header_t *header,
                           const uint8_t *payload) {
  switch (header->type) {
  case MESSAGE_HELLO:
    handle_hello(header->request_id);
    break;
  case MESSAGE_UPLOAD_BEGIN:
    handle_upload_begin(header->request_id, payload, header->payload_size);
    break;
  case MESSAGE_UPLOAD_DATA:
    handle_upload_data(header->request_id, payload, header->payload_size);
    break;
  case MESSAGE_UPLOAD_END:
    finish_upload(header->request_id);
    break;
  case MESSAGE_RUN:
    handle_run(header->request_id, payload, header->payload_size, false);
    break;
  case MESSAGE_DEBUG:
    handle_run(header->request_id, payload, header->payload_size, true);
    break;
  case MESSAGE_PERFORMANCE_GET:
    handle_performance_get(header->request_id, header->flags);
    break;
  case MESSAGE_WIFI_SCAN:
  case MESSAGE_WIFI_SET:
  case MESSAGE_WIFI_FORGET:
    /* Wi-Fi changes would stall an upload in progress. */
    if (!PROTOCOL_HAS_WIFI) {
      send_status(header->request_id, false,
                  "Wi-Fi is not available in this build");
    } else if (g_upload.active) {
      send_status(header->request_id, false, "upload in progress");
    } else if (header->type == MESSAGE_WIFI_SCAN) {
      handle_wifi_scan(header->request_id);
    } else if (header->type == MESSAGE_WIFI_SET) {
      handle_wifi_set(header->request_id, payload, header->payload_size);
    } else {
      handle_wifi_forget(header->request_id, payload, header->payload_size);
    }
    break;
  case MESSAGE_KEY:
    handle_key(header->request_id, payload, header->payload_size);
    break;
  default:
    send_status(header->request_id, false, "unsupported protocol message");
    break;
  }
}

/* Bytes that are not part of a frame are QEMU keyboard input. On hardware
 * the USB console carries no input, so they are discarded. */
static void forward_console_bytes(const uint8_t *bytes, size_t count) {
#if !CONFIG_IDF_TARGET_ESP32C6
  for (size_t index = 0; index < count; ++index) {
    prg32_qemu_input_feed(bytes[index]);
  }
#else
  (void)bytes;
  (void)count;
#endif
}

/* Return true after the eight magic bytes, or false after one idle second so
 * the task can check the upload timeout. */
static bool find_magic(void) {
  size_t matched = 0;
  while (true) {
    uint8_t byte = 0;
    int count = transport_read(&byte, 1,
                               pdMS_TO_TICKS(PROTOCOL_BYTE_TIMEOUT_MS));
    if (count <= 0) {
      /* A partial match followed by silence was ordinary input. */
      forward_console_bytes(PROTOCOL_MAGIC, matched);
      return false;
    }
    if (byte == PROTOCOL_MAGIC[matched]) {
      matched++;
      if (matched == sizeof(PROTOCOL_MAGIC)) {
        return true;
      }
      continue;
    }
    /* No proper prefix of the magic is also its suffix, so a mismatch can
     * only restart the match at this byte. */
    forward_console_bytes(PROTOCOL_MAGIC, matched);
    if (byte == PROTOCOL_MAGIC[0]) {
      matched = 1;
    } else {
      matched = 0;
      forward_console_bytes(&byte, 1);
    }
  }
}

static void protocol_task(void *unused) {
  (void)unused;
  while (true) {
    if (!find_magic()) {
      check_upload_timeout();
      continue;
    }
    protocol_header_t header;
    memcpy(header.magic, PROTOCOL_MAGIC, sizeof(header.magic));
    if (!read_exact(&header.version,
                    sizeof(header) - sizeof(header.magic)) ||
        header.version != PROTOCOL_VERSION ||
        header.payload_size > PROTOCOL_MAX_PAYLOAD) {
      check_upload_timeout();
      continue;
    }
    uint8_t *payload = NULL;
    if (header.payload_size > 0) {
      payload = malloc(header.payload_size);
      if (!payload) {
        send_status(header.request_id, false, "device out of memory");
        continue;
      }
      if (!read_exact(payload, header.payload_size)) {
        free(payload);
        check_upload_timeout();
        continue;
      }
    }
    if (crc32_update(0, payload, header.payload_size) ==
        header.payload_crc32) {
      if (g_upload.active) {
        g_upload.last_frame_ms = prg32_ticks_ms();
      }
      dispatch_frame(&header, payload);
    } else {
      /* Answer instead of staying silent so the host can report the fault
       * immediately rather than after its response timeout. */
      send_status(header.request_id, false, "payload checksum mismatch");
    }
    free(payload);
  }
}

void prg32_usb_protocol_start(void) {
  if (g_transport_ready) {
    return;
  }
#if CONFIG_IDF_TARGET_ESP32C6
  usb_serial_jtag_driver_config_t config = {
      .tx_buffer_size = PROTOCOL_TX_RING_BYTES,
      .rx_buffer_size = PROTOCOL_RX_RING_BYTES,
  };
  if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
    ESP_LOGE(TAG, "could not install USB Serial/JTAG protocol driver");
    return;
  }
  /* Route console output through the same driver ring as protocol frames.
   * Without this the console writes straight to the USB FIFO and log text
   * can be inserted inside a reply frame, breaking its CRC. With no host
   * reading, the console waits once for 50 ms and then drops output. */
  usb_serial_jtag_vfs_use_driver();
#else
  if (!uart_is_driver_installed(UART_NUM_0) &&
      uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0) != ESP_OK) {
    ESP_LOGE(TAG, "could not install QEMU UART protocol driver");
    return;
  }
  /* Same reason as on ESP32-C6: console text goes through the UART driver,
   * whose transmit lock keeps each reply frame whole. */
  uart_vfs_dev_use_driver(UART_NUM_0);
#endif
  if (xTaskCreate(protocol_task, "prg32_usb", PROTOCOL_TASK_STACK, NULL, 5,
                  NULL) != pdPASS) {
    ESP_LOGE(TAG, "could not start runtime protocol task");
    return;
  }
  g_transport_ready = true;
  ESP_LOGI(TAG, "PRG32 runtime upload protocol ready");
}
