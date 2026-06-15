import sys
import os

_IS_TTY = sys.stdout.isatty()
_COLOR_RED = "\x1b[31m" if _IS_TTY else ""
_COLOR_GREEN = "\x1b[32m" if _IS_TTY else ""
_COLOR_YELLOW = "\x1b[33m" if _IS_TTY else ""
_COLOR_CYAN = "\x1b[36m" if _IS_TTY else ""
_COLOR_RESET = "\x1b[0m" if _IS_TTY else ""


def log_info(msg: str):
    print(f"{_COLOR_YELLOW}[INFO]{_COLOR_RESET} {msg}")


def log_ok(msg: str):
    print(f"{_COLOR_GREEN}[OK]{_COLOR_RESET} {msg}")


def log_error(msg: str):
    print(f"{_COLOR_RED}[ERROR]{_COLOR_RESET} {msg}", file=sys.stderr)

def step(msg: str):
    print(f"{_COLOR_CYAN}[STEP]{_COLOR_CYAN} {msg}")


def die(msg: str, code: int = 1):
    log_error(msg)
    sys.exit(code)
