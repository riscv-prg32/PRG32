#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import argparse
from prg32.utilities.env_variables import *
from prg32.utilities.logging import *
from prg32.utilities.partition_handler import read_partition_slot
from prg32.utilities.runtime_handler import validate_cartridge_contract, ensure_cart_max_size

def upload_qemu(args: argparse.Namespace) -> None:
    flash = Path(args.flash)
    cart = Path(args.cartridge)

    if not cart.exists():
        die(f"Error: File does not exist: {cart}")
    if cart.suffix != ".prg32":
        die(f"Error: Input file must be a .prg32 file (got: {cart})")
    data = cart.read_bytes()
    ensure_cart_max_size(data)
    validate_cartridge_contract(data, context="QEMU staging")

    partitions = Path(args.partitions)
    cart_offset, cart_size = read_partition_slot(partitions, args.slot)
    if len(data) > cart_size:
        raise SystemExit(
            f"cartridge is larger than {args.slot} ({cart_size} bytes from {partitions})"
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
                f"{args.slot} requirements ({required} bytes needed)"
            )
        step(f"Injecting cartridge '{cart.name}' into QEMU flash...")
        f.seek(cart_offset)
        f.write(b"\xff" * cart_size)
        f.seek(cart_offset)
        f.write(data)
        log_ok(f"'{args.cartridge}' susccessfuly staged into {flash} at {args.slot}")
        log_info(f"(offset=0x{cart_offset:06x}, size={cart_size})")
