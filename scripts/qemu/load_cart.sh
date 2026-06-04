#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"
. "$ROOT_DIR/scripts/utilities/logging.sh"
. "$ROOT_DIR/scripts/utilities/environment_check.sh"
. "$ROOT_DIR/scripts/qemu/build_qemu.sh"
. "$ROOT_DIR/scripts/cartridge/build_cartridge.sh"

require_cmd() {
  local cmd="$1"
  local hint="$2"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    fail "$cmd not found. $hint"
  fi
}

list_games() {
  step "Listing available games"
  local found=0
  local d
  for d in "$GAMES_DIR"/*; do
    if [[ -d "$d" && -f "$d/graphics/game.S" ]]; then
      basename "$d"
      found=1
    fi
  done

  if [[ "$found" -eq 0 ]]; then
    fail "No games found under $GAMES_DIR"
  fi
}

usage() {
  cat <<'USAGE'
Usage:
  ./scripts/load_cart.sh <game-name>
  ./scripts/load_cart.sh list

Examples:
  ./scripts/load_cart.sh pong
  ./scripts/load_cart.sh list
USAGE
}

main() {
  cd "$ROOT_DIR"

  if [[ "$#" -ne 1 ]]; then
    usage
    exit 1
  fi

  local arg="$1"
  if [[ "$arg" == "list" ]]; then
    list_games
    exit 0
  fi

  check_python
  load_idf_env
  validate_project_layout
  ensure_qemu_flash
  ensure_qemu_efuse
  ensure_qemu_firmware

  # 1. Explicitly construct the source and destination paths
  local source_file="$GAMES_DIR/$arg/graphics/game.S"
  local cart_file="$BUILD_DIR/${arg}.prg32"

  # 2. Invoke the updated build_cartridge with both arguments
  build_cartridge "$source_file" "$cart_file"

  # 3. Inject the compiled cartridge binary
  inject_cartridge "$cart_file"
}

main "$@"