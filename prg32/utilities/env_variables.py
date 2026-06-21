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


# Export some values to the environment for subprocesses that expect them
os.environ.setdefault("BUILD_DIR", ESP32C6_BUILD_DIR)
os.environ.setdefault("QEMU_BUILD_DIR", QEMU_BUILD_DIR)
os.environ.setdefault("QEMU_IMAGE", QEMU_IMAGE)
os.environ.setdefault("QEMU_EFUSE", QEMU_EFUSE)
os.environ.setdefault("QEMU_ELF", QEMU_ELF)