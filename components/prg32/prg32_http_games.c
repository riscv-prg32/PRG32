#include "prg32.h"
#include "prg32_config.h"

#if PRG32_WIFI_ENABLE

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void prg32_http_register_score_handlers(httpd_handle_t server);

static const char *TAG = "prg32_wifi";
static httpd_handle_t server;
enum {
    BMP_HEADER_SIZE = 54,
    BMP_DIB_SIZE = 40,
    BMP_BPP = 24,
    BMP_ROW_SIZE = ((PRG32_LCD_W * 3 + 3) & ~3),
    BMP_IMAGE_SIZE = BMP_ROW_SIZE * PRG32_LCD_H,
    BMP_FILE_SIZE = BMP_HEADER_SIZE + BMP_IMAGE_SIZE,
    SCREENSHOT_ROWS = 1,
};
static uint16_t screenshot_rgb[PRG32_LCD_W * SCREENSHOT_ROWS];
static uint8_t screenshot_band[BMP_ROW_SIZE * SCREENSHOT_ROWS];

static void add_json_u32(cJSON *obj, const char *name, uint32_t value) {
    cJSON_AddNumberToObject(obj, name, (double)value);
}

static esp_err_t send_api_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
                       "{\"ok\":true,"
                       "\"service\":\"PRG32\","
                       "\"endpoints\":["
                       "{\"method\":\"GET\",\"path\":\"/api\",\"available\":true},"
                       "{\"method\":\"GET\",\"path\":\"/api/\",\"available\":true},"
                       "{\"method\":\"GET\",\"path\":\"/api/runtime\",\"available\":true},"
                       "{\"method\":\"GET\",\"path\":\"/api/games\",\"available\":true},"
                       "{\"method\":\"POST\",\"path\":\"/api/games\",\"available\":"
#if PRG32_GAME_UPLOAD_ENABLE
                       "true"
#else
                       "false"
#endif
                       "},"
                       "{\"method\":\"POST\",\"path\":\"/api/games/select\",\"available\":true},"
                       "{\"method\":\"GET\",\"path\":\"/api/screenshot.bmp\",\"available\":true},"
                       "{\"method\":\"GET\",\"path\":\"/api/performance.json\",\"available\":true},"
                       "{\"method\":\"GET\",\"path\":\"/api/scores\",\"available\":"
#if PRG32_WIFI_SCORES_ENABLE
                       "true"
#else
                       "false"
#endif
                       "},"
                       "{\"method\":\"POST\",\"path\":\"/api/scores\",\"available\":"
#if PRG32_WIFI_SCORES_ENABLE
                       "true"
#else
                       "false"
#endif
                       "}"
                       "]}");
    return ESP_OK;
}

static esp_err_t send_runtime(httpd_req_t *req) {
    char json[768];
    prg32_cart_info_t info;
    bool have_cart = prg32_cart_get_info(&info) == 0;
    bool qemu =
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
        true;
#else
        false;
#endif

    int written = snprintf(json,
                           sizeof(json),
                           "{"
                           "\"name\":\"PRG32\","
                           "\"firmware_version\":\"%s\","
                           "\"cart_magic\":\"%s\","
                           "\"cart_abi_major\":%u,"
                           "\"cart_abi_minor\":%u,"
                           "\"cart_load_addr\":%lu,"
                           "\"cart_ram_size\":%lu,"
                           "\"cart_loaded\":%s,"
                           "\"qemu\":%s,"
                           "\"cart\":{"
                           "\"name\":\"%s\","
                           "\"loaded\":%s,"
                           "\"stored\":%s,"
                           "\"code_size\":%lu,"
                           "\"mem_size\":%lu,"
                           "\"audio_size\":%lu,"
                           "\"audio\":%s,"
                           "\"generation\":%lu"
                           "},"
                           "\"diag\":{"
                           "\"frame_count\":%lu,"
                           "\"input_state\":%lu"
                           "}"
                           "}",
                           PRG32_FIRMWARE_VERSION,
                           PRG32_CART_MAGIC,
                           (unsigned)PRG32_CART_ABI_MAJOR,
                           (unsigned)PRG32_CART_ABI_MINOR,
                           (unsigned long)(uint32_t)prg32_cart_load_addr(),
                           (unsigned long)(uint32_t)prg32_cart_ram_size(),
                           have_cart && info.loaded ? "true" : "false",
                           qemu ? "true" : "false",
                           have_cart ? info.name : "",
                           have_cart && info.loaded ? "true" : "false",
                           have_cart && info.stored ? "true" : "false",
                           (unsigned long)(have_cart ? info.code_size : 0),
                           (unsigned long)(have_cart ? info.mem_size : 0),
                           (unsigned long)(have_cart ? info.audio_size : 0),
                           have_cart && info.audio ? "true" : "false",
                           (unsigned long)(have_cart ? info.generation : 0),
                           (unsigned long)prg32_diag_frame_count(),
                           (unsigned long)prg32_diag_input_state());
    if (written < 0 || (size_t)written >= sizeof(json)) {
        httpd_resp_send_err(req, 500, "runtime json too large");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t get_games(httpd_req_t *req) {
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        httpd_resp_send_err(req, 500, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
        prg32_cart_info_t info;
        prg32_cart_get_slot_info(slot, &info);
        cJSON *cart = cJSON_CreateObject();
        if (!cart) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, 500, "out of memory");
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(cart, "slot", info.slot_name);
        cJSON_AddStringToObject(cart, "name", info.name);
        cJSON_AddBoolToObject(cart, "loaded", info.loaded != 0);
        cJSON_AddBoolToObject(cart, "stored", info.stored != 0);
        add_json_u32(cart, "code_size", info.code_size);
        add_json_u32(cart, "mem_size", info.mem_size);
        add_json_u32(cart, "audio_size", info.audio_size);
        cJSON_AddBoolToObject(cart, "audio", info.audio != 0);
        add_json_u32(cart, "generation", info.generation);
        cJSON_AddItemToArray(root, cart);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return ESP_OK;
}

static int request_slot(httpd_req_t *req, uint8_t *slot_out) {
    if (!slot_out) {
        return -1;
    }
    *slot_out = 0;
    char query[48];
    char value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "slot", value, sizeof(value)) == ESP_OK) {
        for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
            prg32_cart_info_t info;
            prg32_cart_get_slot_info(slot, &info);
            if (strcmp(value, info.slot_name) == 0) {
                *slot_out = slot;
                return 0;
            }
        }
        return -1;
    }
    return 0;
}

static void put_le16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void rgb565_to_bgr888(uint16_t color, uint8_t *bgr) {
    uint8_t r5 = (uint8_t)((color >> 11) & 0x1f);
    uint8_t g6 = (uint8_t)((color >> 5) & 0x3f);
    uint8_t b5 = (uint8_t)(color & 0x1f);
    bgr[0] = (uint8_t)((b5 << 3) | (b5 >> 2));
    bgr[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    bgr[2] = (uint8_t)((r5 << 3) | (r5 >> 2));
}

static esp_err_t screenshot_send_all(httpd_req_t *req, int sock, const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    while (length > 0) {
        size_t chunk = length > 64 ? 64 : length;
        int sent = httpd_socket_send(req->handle, sock, (const char *)bytes, chunk, 0);
        if (sent <= 0) {
            return ESP_FAIL;
        }
        bytes += sent;
        length -= (size_t)sent;
    }
    return ESP_OK;
}

static esp_err_t get_screenshot_bmp(httpd_req_t *req) {
    uint8_t header[BMP_HEADER_SIZE] = {0};
    uint16_t *rgb = screenshot_rgb;
    uint8_t *band = screenshot_band;
    esp_err_t err;

    header[0] = 'B';
    header[1] = 'M';
    put_le32(&header[2], BMP_FILE_SIZE);
    put_le32(&header[10], BMP_HEADER_SIZE);
    put_le32(&header[14], BMP_DIB_SIZE);
    put_le32(&header[18], PRG32_LCD_W);
    put_le32(&header[22], PRG32_LCD_H);
    put_le16(&header[26], 1);
    put_le16(&header[28], BMP_BPP);
    put_le32(&header[34], BMP_IMAGE_SIZE);
    put_le32(&header[38], 2835);
    put_le32(&header[42], 2835);

    int sock = httpd_req_to_sockfd(req);
    if (sock < 0) {
        return ESP_FAIL;
    }

    uint8_t first_packet[256];
    int http_header_len = snprintf((char *)first_packet,
                                   sizeof(first_packet) - BMP_HEADER_SIZE,
                                   "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: image/bmp\r\n"
                                   "Content-Length: %u\r\n"
                                   "Content-Disposition: inline; filename=\"screenshot.bmp\"\r\n"
                                   "Cache-Control: no-store\r\n"
                                   "Connection: close\r\n"
                                   "\r\n",
                                   (unsigned)BMP_FILE_SIZE);
    if (http_header_len < 0 ||
        (size_t)http_header_len >= sizeof(first_packet) - BMP_HEADER_SIZE) {
        return ESP_FAIL;
    }
    memcpy(&first_packet[http_header_len], header, BMP_HEADER_SIZE);

    err = screenshot_send_all(req, sock, first_packet, (size_t)http_header_len + BMP_HEADER_SIZE);
    if (err != ESP_OK) {
        goto out;
    }
    for (int y = PRG32_LCD_H - 1; y >= 0;) {
        int first_y = y - SCREENSHOT_ROWS + 1;
        if (first_y < 0) {
            first_y = 0;
        }
        int rows = y - first_y + 1;

        for (int snap_y = first_y; snap_y <= y; ++snap_y) {
            uint16_t *dst = &rgb[(snap_y - first_y) * PRG32_LCD_W];
            if (prg32_gfx_snapshot_row_rgb565(snap_y, dst, PRG32_LCD_W) < 0) {
                err = ESP_FAIL;
                goto out;
            }
        }

        for (int row_index = rows - 1; row_index >= 0; --row_index) {
            const uint16_t *src = &rgb[row_index * PRG32_LCD_W];
            uint8_t *dst = &band[(rows - 1 - row_index) * BMP_ROW_SIZE];
            for (int x = 0; x < PRG32_LCD_W; ++x) {
                rgb565_to_bgr888(src[x], &dst[x * 3]);
            }
        }
        err = screenshot_send_all(req, sock, band, (size_t)rows * BMP_ROW_SIZE);
        if (err != ESP_OK) {
            goto out;
        }
        y = first_y - 1;
    }

out:
    return err;
}

typedef struct {
    httpd_req_t *req;
    char data[1024];
    size_t used;
} performance_http_stream_t;

static int performance_http_flush(performance_http_stream_t *stream) {
    if (!stream || !stream->req || stream->used == 0) {
        return 0;
    }
    esp_err_t err = httpd_resp_send_chunk(stream->req, stream->data, stream->used);
    stream->used = 0;
    return err == ESP_OK ? 0 : -1;
}

static int performance_http_writer(const char *chunk, void *ctx) {
    performance_http_stream_t *stream = (performance_http_stream_t *)ctx;
    if (!stream || !chunk) {
        return -1;
    }

    const char *p = chunk;
    size_t remaining = strlen(chunk);
    while (remaining > 0) {
        size_t space = sizeof(stream->data) - stream->used;
        if (space == 0 && performance_http_flush(stream) != 0) {
            return -1;
        }
        space = sizeof(stream->data) - stream->used;
        size_t n = remaining < space ? remaining : space;
        memcpy(&stream->data[stream->used], p, n);
        stream->used += n;
        p += n;
        remaining -= n;
    }
    return 0;
}

static esp_err_t get_performance_json(httpd_req_t *req) {
    performance_http_stream_t stream = {
        .req = req,
        .used = 0,
    };
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"prg32_performance.json\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    int rc = prg32_performance_json_write(performance_http_writer, &stream);
    if (rc != 0 || performance_http_flush(&stream) != 0) {
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_FAIL;
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t post_game(httpd_req_t *req) {
#if PRG32_GAME_UPLOAD_ENABLE
    ESP_LOGI(TAG, "POST /api/games content_len=%u", (unsigned)req->content_len);
    if (req->content_len == 0 ||
        (size_t)req->content_len > PRG32_CART_RAM_SIZE + sizeof(prg32_cart_header_t)) {
        char msg[96];
        snprintf(msg,
                 sizeof(msg),
                 "invalid cartridge size %u (max %lu)",
                 (unsigned)req->content_len,
                 (unsigned long)(PRG32_CART_RAM_SIZE + sizeof(prg32_cart_header_t)));
        httpd_resp_send_err(req, 400, msg);
        return ESP_FAIL;
    }
    uint8_t *body = malloc((size_t)req->content_len);
    if (!body) {
        httpd_resp_send_err(req, 500, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int n = httpd_req_recv(req,
                               (char *)body + received,
                               req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            free(body);
            httpd_resp_send_err(req, 400, "failed to read cartridge");
            return ESP_FAIL;
        }
        received += (size_t)n;
    }
    ESP_LOGI(TAG, "POST /api/games received=%u", (unsigned)received);
    uint8_t slot = 0;
    if (request_slot(req, &slot) != 0) {
        free(body);
        httpd_resp_send_err(req, 400, "invalid cartridge slot");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "POST /api/games slot=%u", (unsigned)slot);
    ESP_LOGI(TAG, "POST /api/games storing slot=%u", (unsigned)slot);
    int err = prg32_cart_store_slot(slot, body, received);
    free(body);
    if (err != 0) {
        httpd_resp_send_err(req, 400, prg32_cart_last_error());
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "POST /api/games stored slot=%u", (unsigned)slot);
    prg32_cart_info_t info;
    prg32_cart_get_slot_info(slot, &info);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, 500, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "slot", info.slot_name);
    cJSON_AddBoolToObject(root, "stored", info.stored != 0);
    cJSON_AddBoolToObject(root, "loaded", info.loaded != 0);
    cJSON_AddStringToObject(root, "name", info.name);
    add_json_u32(root, "code_size", info.code_size);
    add_json_u32(root, "mem_size", info.mem_size);
    add_json_u32(root, "audio_size", info.audio_size);
    cJSON_AddBoolToObject(root, "audio", info.audio != 0);
    add_json_u32(root, "generation", info.generation);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "POST /api/games sending response");
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_sendstr(req, "{}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return ESP_OK;
#else
    httpd_resp_send_err(req, 403, "game upload disabled");
    return ESP_FAIL;
#endif
}

static esp_err_t select_game(httpd_req_t *req) {
    uint8_t slot = 0;
    if (request_slot(req, &slot) != 0) {
        httpd_resp_send_err(req, 400, "invalid cartridge slot");
        return ESP_FAIL;
    }
    if (prg32_cart_select_slot(slot) != 0) {
        httpd_resp_send_err(req, 400, prg32_cart_last_error());
        return ESP_FAIL;
    }
    prg32_cart_info_t info;
    prg32_cart_get_info(&info);
    char response[64];
    snprintf(response, sizeof(response), "{\"ok\":true,\"slot\":\"%s\"}", info.slot_name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

void prg32_scores_api_start(void) {
    if (server) {
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 60;
    cfg.stack_size = 8192;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        server = NULL;
        return;
    }
    httpd_uri_t rt = {
        .uri = "/api/runtime",
        .method = HTTP_GET,
        .handler = send_runtime
    };
    httpd_uri_t api_root = {
        .uri = "/api/",
        .method = HTTP_GET,
        .handler = send_api_index
    };
    httpd_uri_t api = {
        .uri = "/api",
        .method = HTTP_GET,
        .handler = send_api_index
    };
    httpd_uri_t games_get = {
        .uri = "/api/games",
        .method = HTTP_GET,
        .handler = get_games
    };
    httpd_uri_t games_post = {
        .uri = "/api/games",
        .method = HTTP_POST,
        .handler = post_game
    };
    httpd_uri_t games_select = {
        .uri = "/api/games/select",
        .method = HTTP_POST,
        .handler = select_game
    };
    httpd_uri_t screenshot = {
        .uri = "/api/screenshot.bmp",
        .method = HTTP_GET,
        .handler = get_screenshot_bmp
    };
    httpd_uri_t performance = {
        .uri = "/api/performance.json",
        .method = HTTP_GET,
        .handler = get_performance_json
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api_root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &rt));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &games_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &games_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &games_select));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &screenshot));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &performance));

    prg32_http_register_score_handlers(server);
}

#else

void prg32_scores_api_start(void) {}

#endif
