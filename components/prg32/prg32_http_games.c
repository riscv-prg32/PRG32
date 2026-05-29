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

static void add_json_u32(cJSON *obj, const char *name, uint32_t value) {
    cJSON_AddNumberToObject(obj, name, (double)value);
}

static void add_import(cJSON *imports, const char *name, uintptr_t addr) {
    add_json_u32(imports, name, (uint32_t)addr);
}

static esp_err_t send_runtime(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, 500, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "name", "PRG32");
    cJSON_AddStringToObject(root, "firmware_version", PRG32_FIRMWARE_VERSION);
    cJSON_AddStringToObject(root, "cart_magic", PRG32_CART_MAGIC);
    cJSON_AddNumberToObject(root, "cart_abi_major", PRG32_CART_ABI_MAJOR);
    cJSON_AddNumberToObject(root, "cart_abi_minor", PRG32_CART_ABI_MINOR);
    add_json_u32(root, "cart_load_addr", (uint32_t)prg32_cart_load_addr());
    add_json_u32(root, "cart_ram_size", (uint32_t)prg32_cart_ram_size());
    cJSON_AddBoolToObject(root, "cart_loaded", false);
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
    cJSON_AddBoolToObject(root, "qemu", true);
#else
    cJSON_AddBoolToObject(root, "qemu", false);
#endif

    prg32_cart_info_t info;
    if (prg32_cart_get_info(&info) == 0) {
        cJSON *cart = cJSON_AddObjectToObject(root, "cart");
        cJSON_AddStringToObject(cart, "name", info.name);
        cJSON_AddBoolToObject(cart, "loaded", info.loaded != 0);
        cJSON_AddBoolToObject(cart, "stored", info.stored != 0);
        add_json_u32(cart, "code_size", info.code_size);
        add_json_u32(cart, "mem_size", info.mem_size);
        add_json_u32(cart, "audio_size", info.audio_size);
        add_json_u32(cart, "flags", info.flags);
        cJSON_AddBoolToObject(cart, "audio", info.audio != 0);
        cJSON_AddBoolToObject(cart,
                              "multiplayer",
                              (info.flags & PRG32_CART_FLAG_MULTIPLAYER) != 0);
        add_json_u32(cart, "generation", info.generation);
        cJSON_AddBoolToObject(root, "cart_loaded", info.loaded != 0);
    }

    cJSON *diag = cJSON_AddObjectToObject(root, "diag");
    add_json_u32(diag, "frame_count", prg32_diag_frame_count());
    add_json_u32(diag, "input_state", prg32_diag_input_state());

    cJSON *imports = cJSON_AddObjectToObject(root, "imports");
    add_import(imports, "prg32_ticks_ms", (uintptr_t)prg32_ticks_ms);
    add_import(imports, "prg32_input_read", (uintptr_t)prg32_input_read);
    add_import(imports, "prg32_input_read_player",
               (uintptr_t)prg32_input_read_player);
    add_import(imports, "prg32_input_read_menu",
               (uintptr_t)prg32_input_read_menu);
    add_import(imports, "prg32_controller_read", (uintptr_t)prg32_controller_read);
    add_import(imports, "prg32_audio_beep", (uintptr_t)prg32_audio_beep);
    add_import(imports, "prg32_audio_tone", (uintptr_t)prg32_audio_tone);
    add_import(imports, "prg32_audio_note", (uintptr_t)prg32_audio_note);
    add_import(imports, "prg32_audio_play_notes",
               (uintptr_t)prg32_audio_play_notes);
    add_import(imports, "prg32_audio_sample_u8",
               (uintptr_t)prg32_audio_sample_u8);
    add_import(imports, "prg32_rgb_led_init", (uintptr_t)prg32_rgb_led_init);
    add_import(imports, "prg32_rgb_led_available",
               (uintptr_t)prg32_rgb_led_available);
    add_import(imports, "prg32_rgb_led_set", (uintptr_t)prg32_rgb_led_set);
    add_import(imports, "prg32_rgb_led_off", (uintptr_t)prg32_rgb_led_off);
    add_import(imports, "prg32_rgb_led_vu", (uintptr_t)prg32_rgb_led_vu);
    add_import(imports, "prg32_audio_led_vu_enable",
               (uintptr_t)prg32_audio_led_vu_enable);
    add_import(imports, "prg32_audio_led_vu_enabled",
               (uintptr_t)prg32_audio_led_vu_enabled);
    add_import(imports, "prg32_audio_led_vu_level",
               (uintptr_t)prg32_audio_led_vu_level);
    add_import(imports, "prg32_metrics_init", (uintptr_t)prg32_metrics_init);
    add_import(imports, "prg32_metrics_start_run",
               (uintptr_t)prg32_metrics_start_run);
    add_import(imports, "prg32_metrics_stop_run",
               (uintptr_t)prg32_metrics_stop_run);
    add_import(imports, "prg32_metrics_is_enabled",
               (uintptr_t)prg32_metrics_is_enabled);
    add_import(imports, "prg32_metrics_record", (uintptr_t)prg32_metrics_record);
    add_import(imports, "prg32_metrics_run_id", (uintptr_t)prg32_metrics_run_id);
    add_import(imports, "prg32_performance_test_run",
               (uintptr_t)prg32_performance_test_run);
    add_import(imports, "prg32_performance_has_results",
               (uintptr_t)prg32_performance_has_results);
    add_import(imports, "prg32_performance_summary",
               (uintptr_t)prg32_performance_summary);
    add_import(imports, "prg32_performance_json_write",
               (uintptr_t)prg32_performance_json_write);
    add_import(imports, "prg32_performance_json_alloc",
               (uintptr_t)prg32_performance_json_alloc);
    add_import(imports, "prg32_performance_json_free",
               (uintptr_t)prg32_performance_json_free);
    add_import(imports, "prg32_audio_init", (uintptr_t)prg32_audio_init);
    add_import(imports, "prg32_audio_shutdown", (uintptr_t)prg32_audio_shutdown);
    add_import(imports, "prg32_audio_get_mode", (uintptr_t)prg32_audio_get_mode);
    add_import(imports, "prg32_audio_play_sample",
               (uintptr_t)prg32_audio_play_sample);
    add_import(imports, "prg32_audio_play_sample_pan",
               (uintptr_t)prg32_audio_play_sample_pan);
    add_import(imports, "prg32_audio_stop_channel",
               (uintptr_t)prg32_audio_stop_channel);
    add_import(imports, "prg32_audio_stop_all", (uintptr_t)prg32_audio_stop_all);
    add_import(imports, "prg32_audio_note_on", (uintptr_t)prg32_audio_note_on);
    add_import(imports, "prg32_audio_note_on_pan",
               (uintptr_t)prg32_audio_note_on_pan);
    add_import(imports, "prg32_audio_note_off", (uintptr_t)prg32_audio_note_off);
    add_import(imports, "prg32_audio_play_track",
               (uintptr_t)prg32_audio_play_track);
    add_import(imports, "prg32_audio_stop_track",
               (uintptr_t)prg32_audio_stop_track);
    add_import(imports, "prg32_audio_set_tempo",
               (uintptr_t)prg32_audio_set_tempo);
    add_import(imports, "prg32_audio_set_master_volume",
               (uintptr_t)prg32_audio_set_master_volume);
    add_import(imports, "prg32_audio_set_channel_volume",
               (uintptr_t)prg32_audio_set_channel_volume);
    add_import(imports, "prg32_audio_set_channel_pan",
               (uintptr_t)prg32_audio_set_channel_pan);
    add_import(imports, "prg32_wifi_start_mode",
               (uintptr_t)prg32_wifi_start_mode);
    add_import(imports, "prg32_wifi_current_mode",
               (uintptr_t)prg32_wifi_current_mode);
    add_import(imports, "prg32_wifi_current_ip",
               (uintptr_t)prg32_wifi_current_ip);
    add_import(imports, "prg32_wifi_current_ssid",
               (uintptr_t)prg32_wifi_current_ssid);
    add_import(imports, "prg32_wifi_setup_requested",
               (uintptr_t)prg32_wifi_setup_requested);
    add_import(imports, "prg32_wifi_setup_run",
               (uintptr_t)prg32_wifi_setup_run);
    add_import(imports, "prg32_multiplayer_init",
               (uintptr_t)prg32_multiplayer_init);
    add_import(imports, "prg32_multiplayer_available",
               (uintptr_t)prg32_multiplayer_available);
    add_import(imports, "prg32_multiplayer_join",
               (uintptr_t)prg32_multiplayer_join);
    add_import(imports, "prg32_multiplayer_leave",
               (uintptr_t)prg32_multiplayer_leave);
    add_import(imports, "prg32_multiplayer_tick",
               (uintptr_t)prg32_multiplayer_tick);
    add_import(imports, "prg32_multiplayer_set_local_state",
               (uintptr_t)prg32_multiplayer_set_local_state);
    add_import(imports, "prg32_multiplayer_set_input",
               (uintptr_t)prg32_multiplayer_set_input);
    add_import(imports, "prg32_multiplayer_get_peer_count",
               (uintptr_t)prg32_multiplayer_get_peer_count);
    add_import(imports, "prg32_multiplayer_get_peer",
               (uintptr_t)prg32_multiplayer_get_peer);
    add_import(imports, "prg32_cart_stored_count",
               (uintptr_t)prg32_cart_stored_count);
    add_import(imports, "prg32_cart_get_slot_info",
               (uintptr_t)prg32_cart_get_slot_info);
    add_import(imports, "prg32_cart_select_slot",
               (uintptr_t)prg32_cart_select_slot);
    add_import(imports, "prg32_console_clear", (uintptr_t)prg32_console_clear);
    add_import(imports, "prg32_console_putc", (uintptr_t)prg32_console_putc);
    add_import(imports, "prg32_console_write", (uintptr_t)prg32_console_write);
    add_import(imports, "prg32_console_hex32", (uintptr_t)prg32_console_hex32);
    add_import(imports, "prg32_gfx_clear", (uintptr_t)prg32_gfx_clear);
    add_import(imports, "prg32_gfx_present", (uintptr_t)prg32_gfx_present);
    add_import(imports, "prg32_gfx_lock", (uintptr_t)prg32_gfx_lock);
    add_import(imports, "prg32_gfx_unlock", (uintptr_t)prg32_gfx_unlock);
    add_import(imports, "prg32_gfx_set_fullscreen",
               (uintptr_t)prg32_gfx_set_fullscreen);
    add_import(imports, "prg32_gfx_fullscreen_enabled",
               (uintptr_t)prg32_gfx_fullscreen_enabled);
    add_import(imports, "prg32_gfx_set_band_color",
               (uintptr_t)prg32_gfx_set_band_color);
    add_import(imports, "prg32_gfx_use_background_bands",
               (uintptr_t)prg32_gfx_use_background_bands);
    add_import(imports, "prg32_band_set_mode", (uintptr_t)prg32_band_set_mode);
    add_import(imports, "prg32_band_mode", (uintptr_t)prg32_band_mode);
    add_import(imports, "prg32_band_mode_name", (uintptr_t)prg32_band_mode_name);
    add_import(imports, "prg32_band_set_text", (uintptr_t)prg32_band_set_text);
    add_import(imports, "prg32_band_set_game_info",
               (uintptr_t)prg32_band_set_game_info);
    add_import(imports, "prg32_band_log", (uintptr_t)prg32_band_log);
    add_import(imports, "prg32_band_set_colors",
               (uintptr_t)prg32_band_set_colors);
    add_import(imports, "prg32_band_use_default_colors",
               (uintptr_t)prg32_band_use_default_colors);
    add_import(imports, "prg32_band_load_config",
               (uintptr_t)prg32_band_load_config);
    add_import(imports, "prg32_band_save_config",
               (uintptr_t)prg32_band_save_config);
    add_import(imports, "prg32_gfx_pixel", (uintptr_t)prg32_gfx_pixel);
    add_import(imports, "prg32_gfx_rect", (uintptr_t)prg32_gfx_rect);
    add_import(imports, "prg32_gfx_text8", (uintptr_t)prg32_gfx_text8);
    add_import(imports, "prg32_gfx_snapshot_row_rgb565",
               (uintptr_t)prg32_gfx_snapshot_row_rgb565);
    add_import(imports, "prg32_splash_draw", (uintptr_t)prg32_splash_draw);
    add_import(imports, "prg32_splash_show", (uintptr_t)prg32_splash_show);
    add_import(imports, "prg32_splash_draw_game",
               (uintptr_t)prg32_splash_draw_game);
    add_import(imports, "prg32_splash_show_game",
               (uintptr_t)prg32_splash_show_game);
    add_import(imports, "prg32_splash_show_default",
               (uintptr_t)prg32_splash_show_default);
    add_import(imports, "prg32_debug_overlay_draw", (uintptr_t)prg32_debug_overlay_draw);
    add_import(imports, "prg32_keyboard_init", (uintptr_t)prg32_keyboard_init);
    add_import(imports, "prg32_keyboard_update", (uintptr_t)prg32_keyboard_update);
    add_import(imports, "prg32_keyboard_draw", (uintptr_t)prg32_keyboard_draw);
    add_import(imports, "prg32_text_input", (uintptr_t)prg32_text_input);
    add_import(imports, "prg32_tile_clear", (uintptr_t)prg32_tile_clear);
    add_import(imports, "prg32_tile_define", (uintptr_t)prg32_tile_define);
    add_import(imports, "prg32_tile_put", (uintptr_t)prg32_tile_put);
    add_import(imports, "prg32_tile_present", (uintptr_t)prg32_tile_present);
    add_import(imports, "prg32_playfield_clear", (uintptr_t)prg32_playfield_clear);
    add_import(imports, "prg32_playfield_put", (uintptr_t)prg32_playfield_put);
    add_import(imports, "prg32_playfield_get", (uintptr_t)prg32_playfield_get);
    add_import(imports, "prg32_playfield_scroll", (uintptr_t)prg32_playfield_scroll);
    add_import(imports, "prg32_playfield_scroll_by",
               (uintptr_t)prg32_playfield_scroll_by);
    add_import(imports, "prg32_playfield_parallax",
               (uintptr_t)prg32_playfield_parallax);
    add_import(imports, "prg32_playfield_camera", (uintptr_t)prg32_playfield_camera);
    add_import(imports, "prg32_playfield_camera_x",
               (uintptr_t)prg32_playfield_camera_x);
    add_import(imports, "prg32_playfield_camera_y",
               (uintptr_t)prg32_playfield_camera_y);
    add_import(imports, "prg32_playfield_draw", (uintptr_t)prg32_playfield_draw);
    add_import(imports, "prg32_playfield_draw_dual",
               (uintptr_t)prg32_playfield_draw_dual);
    add_import(imports, "prg32_playfield_present", (uintptr_t)prg32_playfield_present);
    add_import(imports, "prg32_platform_tile_flags",
               (uintptr_t)prg32_platform_tile_flags);
    add_import(imports, "prg32_platform_tile_flags_get",
               (uintptr_t)prg32_platform_tile_flags_get);
    add_import(imports, "prg32_platform_tile_at",
               (uintptr_t)prg32_platform_tile_at);
    add_import(imports, "prg32_platform_solid_at",
               (uintptr_t)prg32_platform_solid_at);
    add_import(imports, "prg32_platform_actor_init",
               (uintptr_t)prg32_platform_actor_init);
    add_import(imports, "prg32_platform_actor_move",
               (uintptr_t)prg32_platform_actor_move);
    add_import(imports, "prg32_platform_actor_step",
               (uintptr_t)prg32_platform_actor_step);
    add_import(imports, "prg32_platform_camera_follow",
               (uintptr_t)prg32_platform_camera_follow);
    add_import(imports, "prg32_sprite_hitbox", (uintptr_t)prg32_sprite_hitbox);
    add_import(imports, "prg32_sprite_draw_8x8", (uintptr_t)prg32_sprite_draw_8x8);
    add_import(imports, "prg32_sprite_draw_16x16", (uintptr_t)prg32_sprite_draw_16x16);
    add_import(imports, "prg32_sprite_anim_frame",
               (uintptr_t)prg32_sprite_anim_frame);
    add_import(imports, "prg32_sprite_draw_frame",
               (uintptr_t)prg32_sprite_draw_frame);
    add_import(imports, "prg32_sprite_anim_init", (uintptr_t)prg32_sprite_anim_init);
    add_import(imports, "prg32_sprite_anim_update",
               (uintptr_t)prg32_sprite_anim_update);
    add_import(imports, "prg32_sprite_anim_draw", (uintptr_t)prg32_sprite_anim_draw);
    add_import(imports, "prg32_score_submit", (uintptr_t)prg32_score_submit);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_sendstr(req, "{}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return ESP_OK;
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
        add_json_u32(cart, "flags", info.flags);
        cJSON_AddBoolToObject(cart, "audio", info.audio != 0);
        cJSON_AddBoolToObject(cart,
                              "multiplayer",
                              (info.flags & PRG32_CART_FLAG_MULTIPLAYER) != 0);
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

static uint8_t request_slot(httpd_req_t *req) {
    char query[48];
    char value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "slot", value, sizeof(value)) == ESP_OK) {
        for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
            prg32_cart_info_t info;
            prg32_cart_get_slot_info(slot, &info);
            if (strcmp(value, info.slot_name) == 0) {
                return slot;
            }
        }
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

static esp_err_t get_screenshot_bmp(httpd_req_t *req) {
    enum {
        BMP_HEADER_SIZE = 54,
        BMP_BPP = 24,
        BMP_ROW_SIZE = ((PRG32_LCD_W * 3 + 3) & ~3),
        BMP_IMAGE_SIZE = BMP_ROW_SIZE * PRG32_LCD_H,
        BMP_FILE_SIZE = BMP_HEADER_SIZE + BMP_IMAGE_SIZE,
    };
    uint8_t header[BMP_HEADER_SIZE] = {0};
    uint16_t rgb[PRG32_LCD_W];
    uint8_t row[BMP_ROW_SIZE];
    esp_err_t err;

    header[0] = 'B';
    header[1] = 'M';
    put_le32(&header[2], BMP_FILE_SIZE);
    put_le32(&header[10], BMP_HEADER_SIZE);
    put_le32(&header[14], 40);
    put_le32(&header[18], PRG32_LCD_W);
    put_le32(&header[22], PRG32_LCD_H);
    put_le16(&header[26], 1);
    put_le16(&header[28], BMP_BPP);
    put_le32(&header[34], BMP_IMAGE_SIZE);
    put_le32(&header[38], 2835);
    put_le32(&header[42], 2835);

    prg32_gfx_lock();
    prg32_gfx_present();

    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=\"screenshot.bmp\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    err = httpd_resp_send_chunk(req, (const char *)header, sizeof(header));
    if (err != ESP_OK) {
        goto out;
    }
    for (int y = PRG32_LCD_H - 1; y >= 0; --y) {
        if (prg32_gfx_snapshot_row_rgb565(y, rgb, PRG32_LCD_W) < 0) {
            httpd_resp_sendstr_chunk(req, NULL);
            err = ESP_FAIL;
            goto out;
        }
        for (int x = 0; x < PRG32_LCD_W; ++x) {
            rgb565_to_bgr888(rgb[x], &row[x * 3]);
        }
        err = httpd_resp_send_chunk(req, (const char *)row, BMP_ROW_SIZE);
        if (err != ESP_OK) {
            goto out;
        }
    }
    err = httpd_resp_sendstr_chunk(req, NULL);

out:
    prg32_gfx_unlock();
    return err;
}

static int http_performance_json_write(const char *chunk, void *ctx) {
    if (!chunk || !ctx) {
        return -1;
    }
    httpd_req_t *req = (httpd_req_t *)ctx;
    return httpd_resp_sendstr_chunk(req, chunk) == ESP_OK ? 0 : -1;
}

static esp_err_t get_performance_json(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"prg32_performance.json\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    int rc = prg32_performance_json_write(http_performance_json_write, req);
    esp_err_t end_err = httpd_resp_sendstr_chunk(req, NULL);
    if (rc != 0) {
        return ESP_FAIL;
    }
    return end_err;
}

static esp_err_t post_game(httpd_req_t *req) {
#if PRG32_GAME_UPLOAD_ENABLE
    if (req->content_len <= 0 ||
        (size_t)req->content_len > PRG32_CART_RAM_SIZE + sizeof(prg32_cart_header_t)) {
        char msg[96];
        snprintf(msg,
                 sizeof(msg),
                 "invalid cartridge size %d (max %lu)",
                 req->content_len,
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
    uint8_t slot = request_slot(req);
    int err = prg32_cart_install_slot(slot, body, received, 1);
    free(body);
    if (err != 0) {
        httpd_resp_send_err(req, 400, prg32_cart_last_error());
        return ESP_FAIL;
    }
    prg32_cart_info_t info;
    prg32_cart_get_info(&info);
    char response[128];
    snprintf(response,
             sizeof(response),
             "{\"ok\":true,\"slot\":\"%s\",\"name\":\"%s\"}",
             info.slot_name,
             info.name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
#else
    httpd_resp_send_err(req, 403, "game upload disabled");
    return ESP_FAIL;
#endif
}

static esp_err_t select_game(httpd_req_t *req) {
    uint8_t slot = request_slot(req);
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
    cfg.max_uri_handlers = 10;
    cfg.recv_wait_timeout = 10;
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
