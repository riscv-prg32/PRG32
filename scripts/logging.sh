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