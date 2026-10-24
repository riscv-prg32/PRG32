""" Cartridge Building for ESP32C6

The cartridge workflow keeps the PRG32 firmware resident on the board. A game
is linked for the firmware's cartridge RAM address and PRG32 API import table,
packed as a .prg32 file, then uploaded over HTTP or staged into QEMU flash.
"""

import urllib.error
import urllib.request
from pathlib import Path
import argparse
import sys
import tempfile
from prg32.utilities.logging import *
from prg32.utilities.env_variables import *
from prg32.utilities.partition_handler import read_partition_slot
from prg32.utilities.runtime_handler import (
    fetch_runtime,
    validate_cartridge_contract,
    ensure_cart_max_size,
    resolve_cart_ram_size,
    run,
)


def upload_esp32c6_usb(args: argparse.Namespace, data: bytes) -> None:
    """Validate and flash one cartridge partition over USB serial."""
    partitions = Path(args.partitions)
    cart_offset, cart_size = read_partition_slot(partitions, args.slot)
    if len(data) > cart_size:
        raise SystemExit(
            f"cartridge is larger than {args.slot} "
            f"({cart_size} bytes from {partitions})"
        )

    cart_ram_size, ram_source = resolve_cart_ram_size(
        args.cart_ram_kib,
        Path("build-esp32c6/sdkconfig"),
    )
    log_info(f"Cartridge RAM limit: {cart_ram_size} bytes ({ram_source})")
    validate_cartridge_contract(
        data,
        runtime={"cart_ram_size": cart_ram_size},
        context="USB upload",
    )

    # Flash the complete partition so bytes from a previously larger package,
    # including an old metadata trailer, cannot remain after the new image.
    with tempfile.NamedTemporaryFile(suffix=".bin") as slot_image:
        slot_image.write(data)
        slot_image.write(b"\xff" * (cart_size - len(data)))
        slot_image.flush()
        run([
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            "esp32c6",
            "--port",
            args.port,
            "--baud",
            args.baud,
            "write_flash",
            f"0x{cart_offset:x}",
            slot_image.name,
        ])
    log_ok(
        f"'{args.cartridge}' uploaded over USB to {args.slot} "
        f"at 0x{cart_offset:06x}"
    )

def upload_esp32c6(args: argparse.Namespace) -> None:
    data = Path(args.cartridge).read_bytes()
    ensure_cart_max_size(data)
    if getattr(args, "port", None):
        upload_esp32c6_usb(args, data)
        return

    runtime = fetch_runtime(args.url)
    validate_cartridge_contract(data, runtime=runtime, context="upload")
    endpoint = args.url.rstrip("/") + "/api/games?slot=" + args.slot
    
    request = urllib.request.Request(
        endpoint,
        data=data,
        method="POST",
        headers={"Content-Type": "application/octet-stream"},
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            print(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"upload failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"upload failed: {exc}") from exc

def run_esp32c6(args: argparse.Namespace) -> None:
    # Construct the endpoint (e.g., http://192.168.4.1/api/games/select?slot=cart0)
    endpoint = args.url.rstrip("/") + "/api/games/select?slot=" + args.slot
    
    # For a POST request with no body, data must be empty bytes b""
    request = urllib.request.Request(
        endpoint,
        data=b"", 
        method="POST"
    )
    
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            print(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"Selection failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"Selection failed: {exc}") from exc
    
def upload_and_run_esp32c6(args: argparse.Namespace):
    upload_esp32c6(args)
    run_esp32c6(args)
    log_ok("Finished upload and run on ESP32C6")
