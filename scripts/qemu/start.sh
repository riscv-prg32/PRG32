#!/usr/bin/env bash
set -euo pipefail

# Get the PRG32 Directory
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"
. "$ROOT_DIR/scripts/utilities/environment_check.sh"
. "$ROOT_DIR/scripts/utilities/logging.sh"
. "$ROOT_DIR/scripts/qemu/launch_qemu.sh"
. "$ROOT_DIR/scripts/qemu/qemu_inject_cartridge.sh"
. "$ROOT_DIR/scripts/cartridge/build_cartridge.sh"


discover_games() {
  GAMES=()

  local d
  for d in "$GAMES_DIR"/*; do
    if [[ -d "$d" ]]; then
      GAMES+=("$(basename "$d")")
    fi
  done

  if [[ "${#GAMES[@]}" -eq 0 ]]; then
    die "No games found in $GAMES_DIR"
  fi
}

print_game_menu() {
  local i

  echo
  echo "Available games:"
  for i in "${!GAMES[@]}"; do
    printf "[%d] %s\n" "$((i + 1))" "${GAMES[$i]}"
  done
  echo
  echo "Type a number to run a game"
  echo "Type r to rerun last game"
  echo "Type q to quit"
}

run_game_flow() {
  local game_name="$1"
# 1. Construct the explicit paths for source and destination
  local source_file="$GAMES_DIR/$game_name/graphics/game.S"
  local cart_file="$BUILD_DIR/${game_name}.prg32"

  ensure_qemu_efuse

  # 2. Invoke the fixed build_cartridge with both arguments
  build_cartridge "$source_file" "$cart_file"
  
  # 3. Inject the compiled cartridge binary
  inject_cartridge "$cart_file"

  launch_qemu
}

main_loop() {
  local last_game=""
  local input=""
  local idx=""

  while true; do
    discover_games
    print_game_menu

    if [[ -n "$last_game" ]]; then
      printf "Selection (last: %s): " "$last_game"
    else
      printf "Selection: "
    fi

    IFS= read -r input

    case "$input" in
      q|Q)
        log_info "Goodbye"
        exit 0
        ;;
      r|R)
        if [[ -z "$last_game" ]]; then
          log_error "No previously run game. Select a game number first."
          continue
        fi
        run_game_flow "$last_game"
        ;;
      ''|*[!0-9]*)
        log_error "Invalid input: $input"
        ;;
      *)
        idx="$((input - 1))"
        if (( idx < 0 || idx >= ${#GAMES[@]} )); then
          log_error "Invalid selection number: $input"
          continue
        fi
        last_game="${GAMES[$idx]}"
        run_game_flow "$last_game"
        ;;
    esac
  done
}

main() {
  cd "$ROOT_DIR"
  log_info "PRG32 QEMU launcher starting"

  check_python
  load_idf_env
  validate_project_layout
  ensure_qemu_flash
  ensure_qemu_efuse
  ensure_qemu_firmware
  main_loop
}

main "$@"
