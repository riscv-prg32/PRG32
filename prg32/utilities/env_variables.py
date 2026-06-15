from pathlib import Path
import os
import struct

# Environment Variables
ROOT_DIR = Path(__file__).resolve().parents[2]
GAMES_DIR = "examples/games"
PRG32_ENTRY = "prg32/prg32.py"
PRG32_ABI_JSON_PATH = ROOT_DIR / "prg32" / "abi" / "prg32_abi.json"

# ESP32C6 environment variables
ESP32C6_BUILD_DIR = "build-esp32c6"
ESP32C6_IMAGE = f"{ESP32C6_BUILD_DIR}/PRG32.bin"
ESP32C6_ELF = f"{ESP32C6_BUILD_DIR}/PRG32.elf"
ESP32C6_SDKCONFIG = f"{ESP32C6_BUILD_DIR}/sdkconfig"
ESP32C6_SDKCONFIG_DEFAULTS = "sdkconfig.defaults"

# QEMU environment variables
QEMU_BUILD_DIR = "build-qemu"
QEMU_IMAGE = f"{QEMU_BUILD_DIR}/qemu_flash.bin"
QEMU_EFUSE = f"{QEMU_BUILD_DIR}/qemu_efuse.bin"
QEMU_ELF = f"{QEMU_BUILD_DIR}/PRG32.elf"
QEMU_SDKCONFIG= f"{QEMU_BUILD_DIR}/sdkconfig"
QEMU_SDKCONFIG_DEFAULTS = "sdkconfig.defaults.qemu"

# Metrics variables
METRICS_SDKCONFIG_DEFAULTS = "sdkconfig.defaults.metrics"

# Target Defaults
TARGET_DEFAULTS = {
    "esp32c6": {
        "firmware_elf": ESP32C6_ELF,
        "build_dir" : ESP32C6_BUILD_DIR
    },
    "qemu": {
        "firmware_elf": QEMU_ELF,
        "build_dir": QEMU_BUILD_DIR
    }
}

# Cartridge and Partitions
CART_HEADER = struct.Struct("<4sHHHHIIIIIII32s")
CART_HEADER_V2 = struct.Struct("<4sHHHHIIIIIII32sIIIIIII")
CART_MAGIC = b"PRG2"
CART_ABI_MAJOR = 1
CART_ABI_MINOR = 0
PRG32_CART_FLAG_AUDIO_BLOCK = 1 << 0
PRG32_CART_FLAG_MULTIPLAYER = 1 << 1
PRG32_CART_FLAG_ABI_TABLE = 1 << 2
PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE = 0
PRG32_IMPORT_MODEL_ABI_TABLE = 1
AUDIO_BLOCK_MAGIC = b"AUD0"
DEFAULT_PARTITION_TABLE = ROOT_DIR / "partitions_prg32.csv"
DEFAULT_CART_SLOT = "cart0"
FALLBACK_CART_RAM_SIZE = 32 * 1024
FALLBACK_CART_LOAD_ADDR = 0x40800000
FLASH_SIZE = 4 * 1024 * 1024

# Framework names
IMPORT_NAMES = [
    "prg32_ticks_ms",
    "prg32_input_read",
    "prg32_input_read_player",
    "prg32_input_read_menu",
    "prg32_controller_read",
    "prg32_audio_beep",
    "prg32_audio_tone",
    "prg32_audio_note",
    "prg32_audio_play_notes",
    "prg32_audio_sample_u8",
    "prg32_audio_init",
    "prg32_audio_shutdown",
    "prg32_audio_get_mode",
    "prg32_audio_play_sample",
    "prg32_audio_play_sample_pan",
    "prg32_audio_stop_channel",
    "prg32_audio_stop_all",
    "prg32_audio_note_on",
    "prg32_audio_note_on_pan",
    "prg32_audio_note_off",
    "prg32_audio_play_track",
    "prg32_audio_stop_track",
    "prg32_audio_set_tempo",
    "prg32_audio_set_master_volume",
    "prg32_audio_set_channel_volume",
    "prg32_audio_set_channel_pan",
    "prg32_wifi_start_mode",
    "prg32_wifi_current_mode",
    "prg32_wifi_current_ip",
    "prg32_wifi_current_ssid",
    "prg32_wifi_setup_requested",
    "prg32_wifi_setup_run",
    "prg32_cart_stored_count",
    "prg32_cart_get_slot_info",
    "prg32_cart_select_slot",
    "prg32_console_clear",
    "prg32_console_putc",
    "prg32_console_write",
    "prg32_console_hex32",
    "prg32_gfx_clear",
    "prg32_gfx_present",
    "prg32_gfx_pixel",
    "prg32_gfx_rect",
    "prg32_gfx_text8",
    "prg32_splash_draw",
    "prg32_splash_show",
    "prg32_splash_show_default",
    "prg32_debug_overlay_draw",
    "prg32_keyboard_init",
    "prg32_keyboard_update",
    "prg32_keyboard_draw",
    "prg32_text_input",
    "prg32_tile_clear",
    "prg32_tile_define",
    "prg32_tile_put",
    "prg32_tile_present",
    "prg32_playfield_clear",
    "prg32_playfield_put",
    "prg32_playfield_get",
    "prg32_playfield_scroll",
    "prg32_playfield_scroll_by",
    "prg32_playfield_parallax",
    "prg32_playfield_camera",
    "prg32_playfield_camera_x",
    "prg32_playfield_camera_y",
    "prg32_playfield_draw",
    "prg32_playfield_draw_dual",
    "prg32_playfield_present",
    "prg32_platform_tile_flags",
    "prg32_platform_tile_flags_get",
    "prg32_platform_tile_at",
    "prg32_platform_solid_at",
    "prg32_platform_actor_init",
    "prg32_platform_actor_move",
    "prg32_platform_actor_step",
    "prg32_platform_camera_follow",
    "prg32_sprite_hitbox",
    "prg32_sprite_draw_8x8",
    "prg32_sprite_draw_16x16",
    "prg32_sprite_anim_frame",
    "prg32_sprite_draw_frame",
    "prg32_sprite_anim_init",
    "prg32_sprite_anim_update",
    "prg32_sprite_anim_draw",
    "prg32_score_submit",
]

# Export some values to the environment for subprocesses that expect them
os.environ.setdefault("BUILD_DIR", ESP32C6_BUILD_DIR)
os.environ.setdefault("QEMU_BUILD_DIR", QEMU_BUILD_DIR)
os.environ.setdefault("QEMU_IMAGE", QEMU_IMAGE)
os.environ.setdefault("QEMU_EFUSE", QEMU_EFUSE)
os.environ.setdefault("QEMU_ELF", QEMU_ELF)