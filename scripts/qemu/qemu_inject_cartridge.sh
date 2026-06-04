#!/usr/bin/env bash

# If INJECTQEMU_SH_INCLUDED is already set, stop reading this file
if [ -n "${INJECTQEMU_SH_INCLUDED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
readonly INJECTQEMU_SH_INCLUDED=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"
. "$ROOT_DIR/scripts/utilities/logging.sh"

inject_cartridge() {
local cart_path="$1"

    # Ensure the file exists and has the correct extension
    if [[ ! -f "$cart_path" ]]; then
        die "Error: File does not exist: $cart_path"
    fi

    if [[ "$cart_path" != *.prg32 ]]; then
        die "Error: Input file must be a .prg32 file (got: $cart_path)"
    fi

    # Extract the file name for cleaner logging output
    local file_name
    file_name=$(basename "$cart_path")

    # Perform the injection
    log_info "Injecting cartridge '$file_name' into QEMU flash..."
    python3 "$GAME_TOOL" upload-qemu "$cart_path" --flash "$QEMU_IMAGE"
    
    log_ok "Cartridge '$file_name' successfully staged in QEMU flash"
}

print_usage() {
    echo "Usage: $0 <path_to_game.prg32>"
}

main() {
if [ "$#" -ne 1 ]; then
        print_usage
        exit 1
    fi

    inject_cartridge "$1"
}

# Only run main if the script is being executed directly, not sourced
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi