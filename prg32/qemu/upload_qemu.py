#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import argparse
from prg32.utilities.env_variables import *
from prg32.utilities.logging import *
from prg32.utilities.partition_handler import read_partition_slot
from prg32.utilities.runtime_handler import (
    ensure_cart_max_size,
    resolve_cart_ram_size,
    validate_cartridge_contract,
)

def inject_cartridge(cartridge: str,
                     flash: str = QEMU_IMAGE,
                     partitions: str = str(DEFAULT_PARTITION_TABLE),
                     slot: str = DEFAULT_CART_SLOT,
                     cart_ram_kib: int | None = None) -> None:
    """Validate a .prg32 cartridge and write it into a QEMU flash slot."""
    flash = Path(flash)
    cart = Path(cartridge)

    if not cart.exists():
        die(f"Error: File does not exist: {cart}")
    if cart.suffix != ".prg32":
        die(f"Error: Input file must be a .prg32 file (got: {cart})")
    data = cart.read_bytes()
    ensure_cart_max_size(data)
    # QEMU has no /api/runtime during staging. Use --cart-ram-kib, else the
    # profile recorded in the sdkconfig next to the flash image, else 64 KiB.
    cart_ram_size, ram_source = resolve_cart_ram_size(
        cart_ram_kib,
        flash.parent / "sdkconfig",
    )
    log_info(f"Cartridge RAM limit: {cart_ram_size} bytes ({ram_source})")
    validate_cartridge_contract(
        data,
        runtime={"cart_ram_size": cart_ram_size},
        context="QEMU staging",
    )

    partitions = Path(partitions)
    cart_offset, cart_size = read_partition_slot(partitions, slot)
    if len(data) > cart_size:
        raise SystemExit(
            f"cartridge is larger than {slot} ({cart_size} bytes from {partitions})"
        )
    if not flash.exists():
        raise SystemExit(f"QEMU flash image not found: {flash}")
    with flash.open("r+b") as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        required = cart_offset + cart_size
        if size < required:
            raise SystemExit(
                "QEMU flash image is smaller than "
                f"{slot} requirements ({required} bytes needed)"
            )
        step(f"Injecting cartridge '{cart.name}' into QEMU flash...")
        f.seek(cart_offset)
        f.write(b"\xff" * cart_size)
        f.seek(cart_offset)
        f.write(data)
        log_ok(f"'{cartridge}' successfully staged into {flash} at {slot}")
        log_info(f"(offset=0x{cart_offset:06x}, size={cart_size})")

def upload_qemu(args: argparse.Namespace) -> None:
    """CLI entry for `python3 -m prg32 qemu upload`."""
    inject_cartridge(
        args.cartridge,
        flash=args.flash,
        partitions=args.partitions,
        slot=args.slot,
        cart_ram_kib=getattr(args, "cart_ram_kib", None),
    )
