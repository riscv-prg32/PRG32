#!/usr/bin/env bash

# Various utilities to check or create files when needed for other scripts.

# If ENVCHECK_SH_INCLUDED is already set, stop reading this file
if [ -n "${ENVCHECK_SH_INCLUDED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
readonly ENVCHECK_SH_INCLUDED=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT_DIR/scripts/utilities/env_variables.sh"

find_idf_root() {
  step "Checking ESP-IDF path"
  local candidate

  if [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/export.sh" ]]; then
    printf '%s\n' "$IDF_PATH"
    return 0
  fi

  for candidate in "$HOME/esp-idf" "$HOME/Desktop/esp-idf"; do
    if [[ -f "$candidate/export.sh" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  # Error
  return 1
}

check_qemu_runtime_deps() {
  local qemu_output
  if ! qemu_output="$(qemu-system-riscv32 -machine help 2>&1)"; then
    if [[ "$qemu_output" == *"libslirp"* ]]; then
      log_error "qemu-system-riscv32 failed due to missing libslirp."
      cat <<'EOF_FIX'
Run:
brew install libslirp
EOF_FIX
      exit 1
    fi
    die "qemu-system-riscv32 failed to start: $qemu_output"
  fi
  log_ok "qemu-system-riscv32 runtime check passed"
}

validate_project_layout() {
  [[ -f "$GAME_TOOL" ]] || die "Missing $GAME_TOOL. Run this script from the PRG32 repository root."
  [[ -d "$GAMES_DIR" ]] || die "Missing $GAMES_DIR. Run this script from the PRG32 repository root."

  if [[ ! -d "$BUILD_DIR" ]]; then
    log_info "$BUILD_DIR is missing; creating it"
    mkdir -p "$BUILD_DIR"
  fi

  log_ok "Project layout looks valid"
}

load_idf_env() {
  # Helper function to prevent repeating the validation checks
  _has_idf_tools() {
    command -v idf.py >/dev/null 2>&1 && command -v riscv32-esp-elf-gcc >/dev/null 2>&1
  }

  if _has_idf_tools; then
    log_ok "ESP-IDF already loaded"
    return 0
  fi

  local idf_root
  if ! idf_root="$(find_idf_root)"; then
    fail "ESP-IDF not loaded and no local checkout was found."
    cat <<'EOF_FIX'
Run one of these commands after installing ESP-IDF:
. ~/esp-idf/export.sh
. ~/Desktop/esp-idf/export.sh
EOF_FIX
    exit 1
  fi

  log_info "Loading ESP-IDF from $idf_root"
  # shellcheck disable=SC1090
  source "$idf_root/export.sh"

  # Re-use the helper function after sourcing
  if ! _has_idf_tools; then
    die "ESP-IDF export loaded from $idf_root, but essential tools (idf.py or riscv32-esp-elf-gcc) are still unavailable."
  fi

  log_ok "ESP-IDF environment loaded"

  check_qemu_runtime_deps
}

check_python() {
  if ! command -v python3 >/dev/null 2>&1; then
    die "python3 not found. Install Python 3 first (for example: brew install python)."
  fi
  log_ok "python3 detected"
}

# If the flash image doesn't exist, an empty flash image is generated
ensure_qemu_flash() {
  step "Ensuring QEMU flash image exists ($QEMU_IMAGE)"

  if [[ ! -f "$QEMU_IMAGE" ]]; then
    log_info "Creating 4MB QEMU flash image"
    mkdir -p "$BUILD_DIR"
    dd if=/dev/zero of="$QEMU_IMAGE" bs=1048576 count=4 >/dev/null 2>&1 || \
      fail "Failed to create $QEMU_IMAGE"
  fi

  local size
  size="$(wc -c < "$QEMU_IMAGE" | tr -d '[:space:]')"
  if [[ "$size" != "$FLASH_SIZE" ]]; then
    fail "$QEMU_IMAGE has size $size bytes, expected $FLASH_SIZE bytes (4MB)."
  fi

  log_info "QEMU flash image ready (4MB)"
}

ensure_qemu_efuse() {
  step "Ensuring QEMU efuse image exists ($QEMU_EFUSE)"

  if [[ ! -f "$QEMU_EFUSE" ]]; then
    python3 - "$QEMU_EFUSE" <<'PY' || fail "Failed to create $QEMU_EFUSE"
from pathlib import Path
import importlib.util
import os
import sys

out_path = Path(sys.argv[1])
qemu_ext_path = Path(os.environ["IDF_PATH"]) / "tools" / "idf_py_actions" / "qemu_ext.py"
spec = importlib.util.spec_from_file_location("qemu_ext", qemu_ext_path)
module = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(module)
out_path.write_bytes(module.QEMU_TARGETS["esp32c3"].default_efuse)
PY
  fi

  log_info "QEMU efuse image ready"
}

ensure_qemu_firmware() {
  if [[ -f "$QEMU_ELF" ]]; then
    log_ok "QEMU firmware already built ($QEMU_ELF)"
    return
  fi

  log_info "QEMU firmware missing; running initial build"
  idf.py -B "$BUILD_DIR" -D "SDKCONFIG=$SDKCONFIG" -D "SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" set-target esp32c3
  idf.py -B "$BUILD_DIR" -D "SDKCONFIG=$SDKCONFIG" -D "SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" build

  [[ -f "$QEMU_ELF" ]] || die "Build finished but $QEMU_ELF is still missing."
  log_ok "QEMU firmware build complete"
}