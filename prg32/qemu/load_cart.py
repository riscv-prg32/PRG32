#!/usr/bin/env python3
import sys
from pathlib import Path
from prg32.utilities.logging import *
from prg32.utilities.environment_check import check_python, load_idf_env, validate_project_layout, ensure_qemu_flash, ensure_qemu_efuse, ensure_qemu_firmware
from prg32.cartridge.build_cartridge import build_cartridge
from prg32.qemu.upload_qemu import inject_cartridge
from prg32.utilities.env_variables import GAMES_DIR, BUILD_DIR


def list_games():
    step("Listing available games")
    found = False
    for d in Path(GAMES_DIR).iterdir():
        if d.is_dir() and (d / "graphics" / "game.S").exists():
            print(d.name)
            found = True
    if not found:
        log_error(f"No games found under {GAMES_DIR}")
        sys.exit(1)


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if len(argv) != 1:
        print("Usage: load_cart.py <game-name>|list")
        sys.exit(1)

    arg = argv[0]
    if arg == "list":
        list_games()
        return

    check_python()
    load_idf_env()
    validate_project_layout()
    ensure_qemu_flash()
    ensure_qemu_efuse()
    ensure_qemu_firmware()

    source_file = Path(GAMES_DIR) / arg / "graphics" / "game.S"
    cart_file = Path(BUILD_DIR) / f"{arg}.prg32"
    build_cartridge(str(source_file), str(cart_file), f"{arg}_graphics")
    inject_cartridge(str(cart_file))


if __name__ == "__main__":
    main()
