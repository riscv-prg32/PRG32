#include "prg32.h"

#include "cJSON.h"
#include "driver/uart.h"
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
#endif

#define PROTOCOL_VERSION 1u
#define PROTOCOL_MAX_PAYLOAD (16u * 1024u)
#define PROTOCOL_CHUNK_MAX 4096u
#define PROTOCOL_TASK_STACK 8192u

enum {
  MESSAGE_HELLO = 0x01,
  MESSAGE_UPLOAD_BEGIN = 0x02,
  MESSAGE_UPLOAD_DATA = 0x03,
  MESSAGE_UPLOAD_END = 0x04,
  MESSAGE_RUN = 0x05,
  MESSAGE_DEBUG = 0x06,
  MESSAGE_PERFORMANCE_GET = 0x07,
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
  char name[33];
  char command[16];
} upload_state_t;

static const char *TAG = "prg32_usb";
static const uint8_t PROTOCOL_MAGIC[8] = {'P', 'R', 'G', '3', '2', 'I', 'O', 0};
static upload_state_t g_upload;
static bool g_transport_ready;
static char *g_performance_json;
static size_t g_performance_offset;

bool prg32_usb_protocol_busy(void) { return g_upload.active; }

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

static int transport_write(const void *data, size_t size) {
#if CONFIG_IDF_TARGET_ESP32C6
  return usb_serial_jtag_write_bytes(data, (uint32_t)size, pdMS_TO_TICKS(2000));
#else
  return uart_write_bytes(UART_NUM_0, data, size);
#endif
}

static bool read_exact(void *data, size_t size) {
  uint8_t *output = data;
  size_t received = 0;
  while (received < size) {
    int count = transport_read(output + received, size - received,
                               pdMS_TO_TICKS(1000));
    if (count < 0) {
      return false;
    }
    received += (size_t)count;
  }
  return true;
}

static bool write_all(const void *data, size_t size) {
  const uint8_t *input = data;
  size_t sent = 0;
  while (sent < size) {
    int count = transport_write(input + sent, size - sent);
    if (count <= 0) {
      return false;
    }
    sent += (size_t)count;
  }
  return true;
}

static void send_frame(uint8_t type, uint16_t flags, uint32_t request_id,
                       const void *payload, size_t payload_size) {
  if (payload_size > PROTOCOL_MAX_PAYLOAD) {
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
  write_all(&header, sizeof(header));
  if (payload_size > 0) {
    write_all(payload, payload_size);
  }
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
    send_frame(MESSAGE_STATUS, 1, request_id, json, strlen(json));
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

static void draw_upload_screen(const char *status, uint16_t color) {
  char slot_line[48];
  char progress_line[48];
  unsigned progress = g_upload.expected_size
                          ? (unsigned)(g_upload.received_size * 100u /
                                       g_upload.expected_size)
                          : 0;
  if (progress > 100) {
    progress = 100;
  }
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
  char json[512];
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"protocol\":\"prg32-io-1.0\","
           "\"capabilities\":[\"upload\",\"run\",\"performance\"],"
           "\"debug_supported\":false,\"runtime\":{"
           "\"cart_ram_size\":%lu,\"cart_max_size\":65536,"
           "\"cart_abi_major\":%u}}",
           (unsigned long)prg32_cart_ram_size(), PRG32_CART_ABI_MAJOR);
  send_frame(MESSAGE_STATUS, 1, request_id, json, strlen(json));
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
  if (!root || !name || !json_u32(root, "size", &image_size) ||
      !json_u32(root, "crc32", &image_crc32) || slot < 0 ||
      image_size == 0 || image_size > PRG32_CART_MAX_SIZE) {
    cJSON_Delete(root);
    send_status(request_id, false, "invalid upload metadata");
    return;
  }

  memset(&g_upload, 0, sizeof(g_upload));
  g_upload.active = true;
  g_upload.slot = (uint8_t)slot;
  g_upload.expected_size = image_size;
  g_upload.expected_crc32 = image_crc32;
  snprintf(g_upload.name, sizeof(g_upload.name), "%.32s", name);
  snprintf(g_upload.command, sizeof(g_upload.command), "%.15s",
           command ? command : "store");
  cJSON_Delete(root);

  prg32_cart_unload();
  if (prg32_cart_stream_begin(g_upload.slot, g_upload.expected_size) != 0) {
    g_upload.active = false;
    draw_upload_screen(prg32_cart_last_error(), PRG32_COLOR_RED);
    send_status(request_id, false, prg32_cart_last_error());
    return;
  }
  draw_upload_screen("UPLOADING...", PRG32_COLOR_CYAN);
  send_status(request_id, true, "upload accepted");
}

static void handle_upload_data(uint32_t request_id, const uint8_t *payload,
                               size_t payload_size) {
  if (!g_upload.active || payload_size < sizeof(uint32_t) ||
      payload_size - sizeof(uint32_t) > PROTOCOL_CHUNK_MAX) {
    send_status(request_id, false, "invalid upload chunk");
    return;
  }
  uint32_t offset = 0;
  memcpy(&offset, payload, sizeof(offset));
  const uint8_t *data = payload + sizeof(offset);
  size_t data_size = payload_size - sizeof(offset);
  if (offset != g_upload.received_size ||
      data_size > g_upload.expected_size - g_upload.received_size) {
    send_status(request_id, false, "out-of-order upload chunk");
    return;
  }
  if (prg32_cart_stream_write(g_upload.slot, offset, data, data_size) != 0) {
    draw_upload_screen(prg32_cart_last_error(), PRG32_COLOR_RED);
    send_status(request_id, false, prg32_cart_last_error());
    return;
  }
  g_upload.running_crc32 =
      crc32_update(g_upload.running_crc32, data, data_size);
  g_upload.received_size += data_size;
  draw_upload_screen("UPLOADING...", PRG32_COLOR_CYAN);
  char response[128];
  snprintf(response, sizeof(response),
           "{\"ok\":true,\"received\":%lu,\"total\":%lu}",
           (unsigned long)g_upload.received_size,
           (unsigned long)g_upload.expected_size);
  send_frame(MESSAGE_STATUS, 1, request_id, response, strlen(response));
}

static void finish_upload(uint32_t request_id) {
  if (!g_upload.active || g_upload.received_size != g_upload.expected_size ||
      g_upload.running_crc32 != g_upload.expected_crc32) {
    draw_upload_screen("UPLOAD FAILED", PRG32_COLOR_RED);
    send_status(request_id, false, "upload length or checksum mismatch");
    vTaskDelay(pdMS_TO_TICKS(1200));
    g_upload.active = false;
    prg32_setup_request();
    return;
  }
  if (prg32_cart_stream_end(g_upload.slot, g_upload.expected_size) != 0) {
    draw_upload_screen("UPLOAD FAILED", PRG32_COLOR_RED);
    send_status(request_id, false, prg32_cart_last_error());
    vTaskDelay(pdMS_TO_TICKS(1200));
    g_upload.active = false;
    prg32_setup_request();
    return;
  }

  bool should_run = strcmp(g_upload.command, "run") == 0 ||
                    strcmp(g_upload.command, "performance") == 0;
  if (strcmp(g_upload.command, "debug") == 0) {
    draw_upload_screen("DEBUG UNSUPPORTED", PRG32_COLOR_RED);
    send_status(request_id, false,
                "debug execution is available in PRG32-Qt only");
    vTaskDelay(pdMS_TO_TICKS(1200));
    g_upload.active = false;
    prg32_setup_request();
    return;
  }
  if (should_run && prg32_cart_select_slot(g_upload.slot) != 0) {
    draw_upload_screen("RUN FAILED", PRG32_COLOR_RED);
    send_status(request_id, false, prg32_cart_last_error());
    vTaskDelay(pdMS_TO_TICKS(1200));
    g_upload.active = false;
    prg32_setup_request();
    return;
  }

  draw_upload_screen("UPLOAD SUCCESSFUL", PRG32_COLOR_GREEN);
  vTaskDelay(pdMS_TO_TICKS(1200));
  g_upload.active = false;
  cJSON *response = cJSON_CreateObject();
  if (response) {
    char slot_name[8];
    snprintf(slot_name, sizeof(slot_name), "cart%u", g_upload.slot);
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "name", g_upload.name);
    cJSON_AddStringToObject(response, "slot", slot_name);
    cJSON_AddBoolToObject(response, "running", should_run);
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json) {
      send_frame(MESSAGE_STATUS, 1, request_id, json, strlen(json));
      cJSON_free(json);
    }
  }
  if (!should_run) {
    prg32_setup_request();
  }
}

static void handle_run(uint32_t request_id, const uint8_t *payload,
                       size_t payload_size, bool debug) {
  if (debug) {
    send_status(request_id, false,
                "debug execution is available in PRG32-Qt only");
    return;
  }
  cJSON *root = cJSON_ParseWithLength((const char *)payload, payload_size);
  int slot = root ? slot_from_name(json_string(root, "slot")) : -1;
  cJSON_Delete(root);
  if (slot < 0 || prg32_cart_select_slot((uint8_t)slot) != 0) {
    send_status(request_id, false,
                slot < 0 ? "invalid cartridge slot" : prg32_cart_last_error());
    return;
  }
  send_status(request_id, true, "cartridge selected");
}

static void handle_performance_get(uint32_t request_id) {
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
  size_t chunk = remaining < PROTOCOL_MAX_PAYLOAD ? remaining
                                                  : PROTOCOL_MAX_PAYLOAD;
  uint16_t flags = chunk == remaining ? 1 : 0;
  send_frame(MESSAGE_RESULT_DATA, flags, request_id,
             g_performance_json + g_performance_offset, chunk);
  g_performance_offset += chunk;
  if (g_performance_offset == total) {
    prg32_performance_json_free(g_performance_json);
    g_performance_json = NULL;
    g_performance_offset = 0;
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
    handle_performance_get(header->request_id);
    break;
  default:
    send_status(header->request_id, false, "unsupported protocol message");
    break;
  }
}

static bool find_magic(void) {
  size_t matched = 0;
  while (true) {
    uint8_t byte = 0;
    if (!read_exact(&byte, 1)) {
      return false;
    }
    if (byte == PROTOCOL_MAGIC[matched]) {
      matched++;
      if (matched == sizeof(PROTOCOL_MAGIC)) {
        return true;
      }
    } else {
#if !CONFIG_IDF_TARGET_ESP32C6
      prg32_qemu_input_feed(byte);
#endif
      matched = byte == PROTOCOL_MAGIC[0] ? 1 : 0;
    }
  }
}

static void protocol_task(void *unused) {
  (void)unused;
  while (true) {
    if (!find_magic()) {
      continue;
    }
    protocol_header_t header;
    memcpy(header.magic, PROTOCOL_MAGIC, sizeof(header.magic));
    if (!read_exact(&header.version,
                    sizeof(header) - sizeof(header.magic)) ||
        header.version != PROTOCOL_VERSION ||
        header.payload_size > PROTOCOL_MAX_PAYLOAD) {
      continue;
    }
    uint8_t *payload = NULL;
    if (header.payload_size > 0) {
      payload = malloc(header.payload_size);
      if (!payload || !read_exact(payload, header.payload_size)) {
        free(payload);
        continue;
      }
    }
    if (crc32_update(0, payload, header.payload_size) ==
        header.payload_crc32) {
      dispatch_frame(&header, payload);
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
      .tx_buffer_size = 4096,
      .rx_buffer_size = 4096,
  };
  if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
    ESP_LOGE(TAG, "could not install USB Serial/JTAG protocol driver");
    return;
  }
#else
  if (!uart_is_driver_installed(UART_NUM_0) &&
      uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0) != ESP_OK) {
    ESP_LOGE(TAG, "could not install QEMU UART protocol driver");
    return;
  }
#endif
  if (xTaskCreate(protocol_task, "prg32_usb", PROTOCOL_TASK_STACK, NULL, 5,
                  NULL) != pdPASS) {
    ESP_LOGE(TAG, "could not start runtime protocol task");
    return;
  }
  g_transport_ready = true;
  ESP_LOGI(TAG, "PRG32 runtime upload protocol ready");
}
