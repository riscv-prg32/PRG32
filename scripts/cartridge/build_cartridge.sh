#!/usr/bin/env bash

# Script that builds an assembly file into a .prg32 cartridge binary

# If BUILDCARTRIDGE_SH_INCLUDED is already set, stop reading this file
if [ -n "${BUILDCARTRIDGE_SH_INCLUDED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
readonly BUILDCARTRIDGE_SH_INCLUDED=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"
. "$ROOT_DIR/scripts/utilities/logging.sh"

build_cartridge() {
    local source_path="$1"
    local out_path="$2"

    # Validation
    [[ -f "$source_path" ]] || die "Missing game source: $source_path"

    # Extract game name dynamically from the output file (e.g., "pong.prg32" -> "pong")
    local filename
    filename=$(basename "$out_path")
    local game_name="${filename%.*}"

    log_info "Building cartridge: $game_name"
    
    # 3. Execute the compilation
    CPATH="$BUILD_DIR/config:$BUILD_DIR:${CPATH:+:$CPATH}" \
    python3 "$GAME_TOOL" build \
        "$source_path" \
        --firmware-elf "$QEMU_ELF" \
        --name "$game_name" \
        --out "$out_path"

    [[ -s "$out_path" ]] || die "Cartridge build failed: $out_path was not created or is empty"
    log_ok "Cartridge created: $out_path"
}

print_usage() {
    echo "Usage: $0 <path_to_source.S|c> <path_to_output.prg32>"
}

main() {
    set -e 
    
    # Expecting exactly 2 arguments now
    if [ "$#" -ne 2 ]; then
        print_usage
        exit 1
    fi

    build_cartridge "$1" "$2"
}

# Only run main if the script is being executed directly, not sourced
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi