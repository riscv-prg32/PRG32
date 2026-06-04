#!/usr/bin/env bash

# If BUILDQEMU_SH_INCLUDED is already set, stop reading this file
if [ -n "${BUILDQEMU_SH_INCLUDED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
readonly BUILDQEMU_SH_INCLUDED=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"
. "$ROOT_DIR/scripts/utilities/logging.sh"

build_qemu() {
    step "Configuring QEMU target (esp32c3)"
    idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3

    step "Building QEMU firmware and flash_image"
    idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
    
    [[ -f "$BUILD_DIR/PRG32.elf" ]] || die "Missing $BUILD_DIR/PRG32.elf after build."
    log_info "Firmware build ready"
    step "Running QEMU"
}

main() {
    set -e
    build_qemu
}

# Only run main if the script is being executed directly, not sourced
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi