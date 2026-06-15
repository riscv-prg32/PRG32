#!/usr/bin/env python3
import sys
from pathlib import Path
from prg32.utilities.logging import *
from prg32.utilities.environment_check import check_python, load_idf_env, validate_project_layout, ensure_qemu_flash, ensure_qemu_efuse, ensure_qemu_firmware
from prg32.qemu.launch_qemu import launch_qemu
from prg32.qemu.upload_qemu import inject_cartridge
from prg32.cartridge.build_cartridge import build_cartridge
from prg32.utilities.env_variables import GAMES_DIR, BUILD_DIR


def discover_games():
    games = []
    for d in Path(GAMES_DIR).iterdir():
        if d.is_dir():
            games.append(d.name)
    if not games:
        raise SystemExit(f"No games found in {GAMES_DIR}")
    return games


def print_game_menu(games):
    print()
    print("Available games:")
    for i, g in enumerate(games, start=1):
        print(f"[{i}] {g}")
    print()
    print("Type a number to run a game")
    print("Type r to rerun last game")
    print("Type q to quit")


def run_game_flow(game_name: str):
    source_file = Path(GAMES_DIR) / game_name / "graphics" / "game.S"
    cart_file = Path(BUILD_DIR) / f"{game_name}.prg32"
    ensure_qemu_efuse()
    build_cartridge(str(source_file), str(cart_file), f"{game_name}_graphics")
    inject_cartridge(str(cart_file))
    launch_qemu()


def main():
    from prg32.utilities.environment_check import ensure_qemu_efuse
    import os
    os.chdir(Path(__file__).resolve().parents[2])
    log_info("PRG32 QEMU launcher starting")
    check_python()
    load_idf_env()
    validate_project_layout()
    ensure_qemu_flash()
    ensure_qemu_efuse()
    ensure_qemu_firmware()

    last_game = ""
    while True:
        games = discover_games()
        print_game_menu(games)
        prompt = f"Selection (last: {last_game}): " if last_game else "Selection: "
        try:
            input_line = input(prompt).strip()
        except (EOFError, KeyboardInterrupt):
            log_info("Goodbye")
            raise SystemExit(0)
        if input_line.lower() == 'q':
            log_info("Goodbye")
            raise SystemExit(0)
        if input_line.lower() == 'r':
            if not last_game:
                log_error("No previously run game. Select a game number first.")
                continue
            run_game_flow(last_game)
        elif input_line.isdigit():
            idx = int(input_line) - 1
            if idx < 0 or idx >= len(games):
                log_error(f"Invalid selection number: {input_line}")
                continue
            last_game = games[idx]
            run_game_flow(last_game)
        else:
            log_error(f"Invalid input: {input_line}")


if __name__ == "__main__":
    main()
