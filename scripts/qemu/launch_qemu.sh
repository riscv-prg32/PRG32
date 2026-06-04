#!/usr/bin/env bash

# If LAUNCHQEMU_SH_INCLUDED is already set, stop reading this file
if [ -n "${LAUNCHQEMU_SH_INCLUDED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
readonly LAUNCHQEMU_SH_INCLUDED=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"
. "$ROOT_DIR/scripts/utilities/logging.sh"

launch_qemu() {
  log_info "Launching QEMU"
  log_info "Press Ctrl + ] to exit"
  exec qemu-system-riscv32 \
    -M esp32c3 \
    -m 4M \
    -drive "file=$QEMU_IMAGE,if=mtd,format=raw" \
    -drive "file=$QEMU_EFUSE,if=none,format=raw,id=efuse" \
    -global "driver=nvram.esp32c3.efuse,property=drive,value=efuse" \
    -global "driver=timer.esp32c3.timg,property=wdt_disable,value=true" \
    -nic user,model=open_eth \
    -display sdl \
    -serial mon:stdio
}

main() {
    set -e
    launch_qemu
}

# Only run main if the script is being executed directly, not sourced
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi