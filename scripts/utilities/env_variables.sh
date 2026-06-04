#!/usr/bin/env bash

# Script that sets environment variables needed by other scripts.

# If ENVVARS_SH_INCLUDED is already set, stop reading this file
if [ -n "${ENVVARS_SH_INCLUDED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
readonly ENVVARS_SH_INCLUDED=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export BUILD_DIR="build-qemu"
export GAMES_DIR="examples/games"
export GAME_TOOL="tools/prg32_game.py"
export QEMU_IMAGE="$BUILD_DIR/flash_image.bin"
export QEMU_EFUSE="$BUILD_DIR/qemu_efuse.bin"
export QEMU_ELF="$BUILD_DIR/PRG32.elf"
export SDKCONFIG="$BUILD_DIR/sdkconfig"
export SDKCONFIG_DEFAULTS="sdkconfig.defaults.qemu"
export FLASH_SIZE=$((4 * 1024 * 1024))