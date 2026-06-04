COLOR_RESET=""
COLOR_RED=""
COLOR_GREEN=""
COLOR_YELLOW=""

if [[ -t 1 ]] && command -v tput >/dev/null 2>&1; then
  if [[ "$(tput colors 2>/dev/null || echo 0)" -ge 8 ]]; then
    COLOR_RESET="$(tput sgr0)"
    COLOR_RED="$(tput setaf 1)"
    COLOR_GREEN="$(tput setaf 2)"
    COLOR_YELLOW="$(tput setaf 3)"
  fi
fi

log_info() {
  echo "${COLOR_YELLOW}[INFO]${COLOR_RESET} $1"
}

log_ok() {
  echo "${COLOR_GREEN}[OK]${COLOR_RESET} $1"
}

log_error() {
  echo "${COLOR_RED}[ERROR]${COLOR_RESET} $1" >&2
}

step() {
  echo "${COLOR_YELLOW}[INFO]${COLOR_RESET} $1"
}

fail() {
  echo "[FAIL] $1" >&2
  exit 1
}

die() {
  log_error "$1"
  exit 1
}