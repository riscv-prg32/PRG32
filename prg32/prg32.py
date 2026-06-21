#!/usr/bin/env python3
"""Main entry for PRG32"""

from __future__ import annotations
import argparse
import sys
from prg32.utilities.env_variables import *
from prg32.utilities.environment_check import doctor
from prg32.utilities.runtime_handler import runtime

from prg32.cartridge.build_cartridge import build_cartridge_cli

from prg32.esp32c6.build_esp32c6 import set_target_esp32c6, build_esp32c6, flash_esp32c6, build_and_flash_esp32c6, reset_esp32c6, erase_flash_esp32c6
from prg32.esp32c6.upload_esp32c6 import upload_esp32c6, run_esp32c6, upload_and_run_esp32c6
from prg32.esp32c6.prepare_legacy import prepare_legacy_esp32c6
from prg32.esp32c6.flash_legacy import flash_legacy_esp32c6


from prg32.qemu.launch_qemu import launch_qemu
from prg32.qemu.upload_qemu import upload_qemu
from prg32.qemu.build_qemu import build_qemu, flash_qemu, build_and_flash_qemu, set_target_qemu

from prg32.store.metadata import attach_metadata, inspect_metadata
from prg32.store.store_api import store_discover, store_list, store_download
from prg32.store.publish import publish, pack_bundle, publish_bundle
from prg32.store.utils import ARCHITECTURE_PROFILES

from prg32.abi.abi_gen import abi_gen_cmd, abi_check_cmd

def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    # ==========================================
    # 'esp32c6' Subcommand Menu
    # ==========================================
    esp32c6_p = sub.add_parser("esp32c6", help="ESP32C6 SoC tasks")
    # Add a subparser tracker specifically for sub-commands of qemu
    esp32c6_sub = esp32c6_p.add_subparsers(dest="sub_cmd", required=True)

    p = esp32c6_sub.add_parser("set-target", help="set the build target to ESP32C6")
    p.set_defaults(func=set_target_esp32c6)

    p = esp32c6_sub.add_parser("build", help="build the ESP32C6 firmware")
    p.add_argument("--skip-target", action="store_true", 
        help="Skip setting target to ESP32C6 (it takes less time to build and it is useless if you have set the target before)"
    )
    p.set_defaults(func=build_esp32c6)

    p = esp32c6_sub.add_parser("flash", help="flash the ESP32C6 image. Does not do anything when a cartridge is running!")
    p.set_defaults(func=flash_esp32c6)

    p = esp32c6_sub.add_parser("build-and-flash", help="build and flash the ESP32C6. Does not do anything when a cartridge is running (?)")
    p.add_argument("--skip-target", action="store_true", 
        help="Skip setting target to ESP32C6 (it takes less time to build and it is useless if you have set the target before)"
    )
    p.set_defaults(func=build_and_flash_esp32c6)

    p = esp32c6_sub.add_parser("upload", 
        help="upload a cartridge to the ESP32C6 SoC over HTTP", 
        usage="%(prog)s CARTRIDGE [options]"
    )
    p.add_argument("cartridge", help="Path to the compiled cartridge file (.prg32)")
    p.add_argument("--url", default="http://192.168.4.1", help="URL of the ESP32C6 device")
    p.add_argument("--slot", default=DEFAULT_CART_SLOT, help="Partition slot to upload into")
    p.set_defaults(func=upload_esp32c6)

    p = esp32c6_sub.add_parser("run", help="run a previously loaded cartridge on the ESP32C6 SoC over HTTP. Does not work when a cartridge is running!", usage="%(prog)s [options]")
    p.add_argument("--url", default="http://192.168.4.1", help="URL of the ESP32C6 device")
    p.add_argument("--slot", default=DEFAULT_CART_SLOT, help="Partition slot to run")
    p.set_defaults(func=run_esp32c6)

    p = esp32c6_sub.add_parser("upload-and-run", help="upload and run a cartridge on the ESP32C6 SoC over HTTP. Does not work when a cartridge is running!", usage="%(prog)s CARTRIDGE [options]")
    p.add_argument("cartridge", help="Path to the compiled cartridge file (.prg32)")
    p.add_argument("--url", default="http://192.168.4.1", help="URL of the ESP32C6 device")
    p.add_argument("--slot", default=DEFAULT_CART_SLOT, help="Partition slot to upload and run into")
    p.set_defaults(func=upload_and_run_esp32c6)

    p = esp32c6_sub.add_parser("erase-flash", help=f"erase flash of ESP32C6 over USB")
    p.set_defaults(func=erase_flash_esp32c6)

    p = esp32c6_sub.add_parser("reset", help=f"erase flash and re-flash {ESP32C6_IMAGE} to ESP32C6 over USB")
    p.set_defaults(func=reset_esp32c6)

    p = esp32c6_sub.add_parser("prepare-legacy", help="prepare a single-file legacy PRG32 firmware image for publishing", usage="%(prog)s [options]")
    p.add_argument("--build-dir", default="build-esp32c6", help="Path to the build directory")
    p.add_argument("--out-dir", default="publish/legacy-firmware", help="Path to the output directory")
    p.add_argument("--name", default="PRG32-legacy-esp32c6", help="Name of the output binary")
    p.add_argument("--skip-build", action="store_true", help="Skip the ESP-IDF build step")
    p.set_defaults(func=prepare_legacy_esp32c6)

    p = esp32c6_sub.add_parser("flash-legacy", help="flash a published single-file legacy PRG32 firmware image", usage="%(prog)s MANIFEST --port PORT [options]")
    p.add_argument("manifest", help="JSON produced by prepare-legacy")
    p.add_argument("--port", required=True, help="Serial port to flash to")
    p.add_argument("--baud", default="460800", help="Baud rate for flashing")
    p.set_defaults(func=flash_legacy_esp32c6)

    # ==========================================
    # 'qemu' Subcommand Menu
    # ==========================================
    qemu_p = sub.add_parser("qemu", help="QEMU emulator tasks")
    # Add a subparser tracker specifically for sub-commands of qemu
    qemu_sub = qemu_p.add_subparsers(dest="sub_cmd", required=True)
    
    p =  qemu_sub.add_parser("set-target", help="flash QEMU")
    p.set_defaults(func=set_target_qemu)
    
    p =  qemu_sub.add_parser("build", help="build QEMU")
    p.add_argument("--skip-target", action="store_true", 
        help="Skip setting target to ESP32C3 (it takes less time to build and it is useless if you have set the target before)"
    )
    p.set_defaults(func=build_qemu)

    p =  qemu_sub.add_parser("run", help="flash QEMU")
    p.set_defaults(func=flash_qemu)

    p =  qemu_sub.add_parser("build-and-run", help="build and flash QEMU")
    p.add_argument("--skip-target", action="store_true", 
        help="Skip setting target to ESP32C3 (it takes less time to build and it is useless if you have set the target before)"
    )
    p.set_defaults(func=build_and_flash_qemu)

    p = qemu_sub.add_parser("upload", 
        help="stage a cartridge into QEMU flash",
        usage="%(prog)s CARTRIDGE [options]"
    )
    p.add_argument("cartridge", help="Path to the compiled cartridge file (.prg32)")
    p.add_argument("--flash", default=QEMU_IMAGE, help="Path to the QEMU flash image")
    p.add_argument("--partitions", default=str(DEFAULT_PARTITION_TABLE), help="Path to the partition table CSV")
    p.add_argument("--slot", default=DEFAULT_CART_SLOT, help="Partition slot to stage into")
    p.set_defaults(func=upload_qemu)

    p = qemu_sub.add_parser("launch", help="launch the QEMU emulator environment")
    p.set_defaults(func=launch_qemu)

    # ==========================================
    # GLOBAL COMMANDS
    # ==========================================
    p = sub.add_parser(
        "build-cartridge", 
        help="build a .prg32 cartridge from assembly or C",
        # You choose either --target OR --firmware-elf
        usage="%(prog)s SOURCE_PATH --out OUT_PATH --name NAME --entry-prefix PREFIX [options]"
    )
    p.add_argument("source", help="Source file path (.c or .S)")
    p.add_argument("--out", required=True, help="Output path for the compiled cartridge (.prg32)")
    p.add_argument("--name", required=True, help="Name of the cartridge")
    p.add_argument("--entry-prefix", required=True, help="Prefix for the entry point functions (e.g., 'pong')")
    p.add_argument("--runtime-url", help="URL of a device to fetch runtime metadata from")
    p.add_argument("--build-dir", help="Directory for intermediate build files")
    p.add_argument(
        "--audio-block",
        help="optional PRG32 AUDIO block produced by tools/prg32audio_pack.py",
    )
    p.add_argument("--multiplayer", action="store_true", help="Enable multiplayer support for the cartridge")
    p.add_argument("--portable", action="store_true", help="Build a portable cartridge using the ABI table")
    p.add_argument("--legacy-absolute-imports", action="store_true", help="Build using legacy absolute memory imports")
    p.add_argument("--required-feature", action="append", default=[], help="Specify a required PRG32 hardware feature")
    p.add_argument("--optional-feature", action="append", default=[], help="Specify an optional PRG32 hardware feature")
    p.add_argument("--march", default="rv32imc_zicsr_zifencei", help="RISC-V architecture string")
    p.add_argument("--mabi", default="ilp32", help="RISC-V ABI string")
    p.add_argument("--tool-prefix", default="riscv32-esp-elf-", help="Prefix for the RISC-V GCC toolchain")
    arch_group = p.add_mutually_exclusive_group(required=False)
    arch_group.add_argument("--architecture", choices=["esp32c6", "qemu"], help="Target architecture (esp32c6 or qemu)")
    arch_group.add_argument("--firmware-elf", help="Path to custom firmware ELF (disables portable build by default)")

    p.set_defaults(func=build_cartridge_cli)

    # ==========================================
    # 'abi' Subcommand Menu
    # ==========================================
    abi_p = sub.add_parser("abi", help="ABI tooling tasks")
    abi_sub = abi_p.add_subparsers(dest="sub_cmd", required=True)

    p = abi_sub.add_parser("gen", help="generate PRG32 portable cartridge ABI files")
    p.set_defaults(func=abi_gen_cmd)

    p = abi_sub.add_parser("check", help="check that PRG32 portable cartridge ABI files are up to date")
    p.set_defaults(func=abi_check_cmd)

    # ==========================================
    # 'store' Subcommand Menu
    # ==========================================
    store_p = sub.add_parser("store", help="CartridgeStore and metadata tasks")
    store_sub = store_p.add_subparsers(dest="sub_cmd", required=True)

    p = store_sub.add_parser(
        "attach-metadata",
        help="append or replace a PRG32META metadata trailer",
        usage="%(prog)s CARTRIDGE --out OUT_PATH --metadata METADATA --icon ICON [options]"
    )
    p.add_argument("cartridge", help="Path to the target cartridge file")
    p.add_argument("--out", required=True, help="Path to write the updated cartridge")
    p.add_argument("--metadata", required=True, help="prg32-metadata-1.0 JSON")
    p.add_argument("--icon", required=True, help="PNG or JPEG icon image")
    p.add_argument("--screenshot", help="optional PNG or JPEG screenshot image")
    p.add_argument("--signature", help="optional signature bytes or JSON object")
    p.add_argument("--colophon", help="optional prg32-colophon-1.0 JSON")
    p.add_argument(
        "--architecture",
        choices=sorted(ARCHITECTURE_PROFILES),
        help="cartridge architecture variant recorded in metadata.runtime",
    )
    p.set_defaults(func=attach_metadata)

    p = store_sub.add_parser(
        "inspect-metadata",
        help="print the PRG32META trailer summary for a cartridge",
        usage="%(prog)s CARTRIDGE [options]"
    )
    p.add_argument("cartridge", help="Path to the target cartridge file")
    p.set_defaults(func=inspect_metadata)

    p = store_sub.add_parser("discover", help="find CartridgeStore instances with mDNS", usage="%(prog)s [options]")
    p.add_argument("--timeout", type=float, default=5, help="mDNS discovery timeout in seconds")
    p.set_defaults(func=store_discover)

    p = store_sub.add_parser("list", help="list cartridges from a CartridgeStore", usage="%(prog)s [options]")
    p.add_argument("--store-url", help="URL of the CartridgeStore")
    p.add_argument("--architecture", choices=sorted(ARCHITECTURE_PROFILES), help="Filter by architecture")
    p.set_defaults(func=store_list)

    p = store_sub.add_parser("download", help="download a cartridge from a CartridgeStore", usage="%(prog)s GAME_ID --architecture ARCH --out OUT_PATH [options]")
    p.add_argument("game_id", help="ID of the game to download")
    p.add_argument("--store-url", help="URL of the CartridgeStore")
    p.add_argument("--architecture", required=True, choices=sorted(ARCHITECTURE_PROFILES), help="Target architecture profile")
    p.add_argument("--version", help="Specific version of the game to download")
    p.add_argument("--out", required=True, help="Output path for the downloaded cartridge")
    p.set_defaults(func=store_download)

    p = store_sub.add_parser("publish", help="build and publish a cartridge bundle", usage="%(prog)s SOURCE --firmware-elf ELF --entry-prefix PREFIX --name NAME [options]")
    p.add_argument("source", help="Source directory containing the game files")
    p.add_argument("--firmware-elf", required=True, help="Path to the runtime firmware ELF")
    p.add_argument("--entry-prefix", required=True, help="Prefix for the entry point functions")
    p.add_argument("--name", required=True, help="Name of the game")
    p.add_argument("--store-url", help="URL of the CartridgeStore to publish to")
    p.add_argument("--architecture", choices=sorted(ARCHITECTURE_PROFILES), help="Target architecture profile")
    p.add_argument("--id", help="Unique ID for the published game")
    p.add_argument("--version", default="1.0.0", help="Version of the published game")
    p.add_argument("--summary", help="Short summary description of the game")
    p.add_argument("--tags", help="Comma-separated list of tags")
    p.add_argument("--icon", help="Path to a PNG or JPEG icon")
    p.add_argument("--splash", help="Path to a PNG or JPEG splash image")
    p.add_argument("--colophon", help="Path to a prg32-colophon-1.0 JSON file")
    p.add_argument("--token", help="Authentication token for the store")
    p.set_defaults(func=publish)

    p = store_sub.add_parser("pack-bundle", help="pack a flat CartridgeStore zip bundle", usage="%(prog)s --manifest MANIFEST --out OUT_PATH [options]")
    p.add_argument("--manifest", required=True, help="Path to the bundle manifest JSON")
    p.add_argument("--out", required=True, help="Output path for the bundle zip")
    p.set_defaults(func=pack_bundle)

    p = store_sub.add_parser("publish-bundle", help="publish a CartridgeStore zip bundle", usage="%(prog)s BUNDLE [options]")
    p.add_argument("bundle", help="Path to the bundle zip file")
    p.add_argument("--store-url", help="URL of the CartridgeStore")
    p.add_argument("--token", help="Authentication token for the store")
    p.set_defaults(func=publish_bundle)


    p = sub.add_parser("doctor", help="check local toolchain prerequisites", usage="%(prog)s [options]")
    p.add_argument("--partitions", default=str(DEFAULT_PARTITION_TABLE), help="Path to the partition table CSV")
    p.add_argument("--slot", default=DEFAULT_CART_SLOT, help="Partition slot to check")
    p.add_argument(
        "--host-only",
        action="store_true",
        help="skip ESP-IDF and RISC-V toolchain checks for CI/unit-test hosts",
    )
    p.add_argument("--tool-prefix", default="riscv32-esp-elf-", help="Prefix for the toolchain")
    p.set_defaults(func=doctor)

    p = sub.add_parser("runtime", help="print runtime linker information", usage="%(prog)s [options]")
    p.add_argument("--url", help="URL of a device to fetch runtime metadata from")
    p.add_argument("--firmware-elf", help="Path to the firmware ELF to analyze")
    p.add_argument("--tool-prefix", default="riscv32-esp-elf-", help="Prefix for the toolchain")
    p.set_defaults(func=runtime)

    args = parser.parse_args(argv)
    args.func(args)
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
