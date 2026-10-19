""" Cartridge Building for ESP32C6

The cartridge workflow keeps the PRG32 firmware resident on the board. A game
is linked for the firmware's cartridge RAM address and PRG32 API import table,
packed as a .prg32 file, then uploaded over HTTP or staged into QEMU flash.
"""

import urllib.error
import urllib.request
from pathlib import Path
import argparse
from prg32.utilities.logging import *
from prg32.utilities.env_variables import *
from prg32.utilities.partition_handler import read_partition_slot
from prg32.utilities.runtime_handler import (
    fetch_runtime,
    validate_cartridge_contract,
    ensure_cart_max_size,
    resolve_cart_ram_size,
)
from prg32.usb_protocol import DEBUG, RUN, ProtocolClient, hello, open_stream, upload_cartridge


def upload_esp32c6_usb(args: argparse.Namespace, data: bytes) -> None:
    """Validate and upload one cartridge through the running PRG32 firmware."""
    partitions = Path(args.partitions)
    _cart_offset, cart_size = read_partition_slot(partitions, args.slot)
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

    command = getattr(args, "command", "store")
    stream = open_stream(args.port, int(args.baud), float(args.timeout))
    client = ProtocolClient(stream, timeout=float(args.timeout))
    try:
        device = hello(client)
        log_info(
            f"Device: {device.get('protocol', 'unknown protocol')}, "
            f"firmware {device.get('firmware', 'unknown')}, "
            f"target {device.get('target', 'unknown')}"
        )
        # Refuse unsupported operations before the device erases the slot.
        if command == "debug" and not device.get("debug_supported", False):
            raise SystemExit(
                "this device cannot instruction-debug cartridges; use PRG32-Qt, "
                "or debug native ESP32-C6 code with `cartridge debug`"
            )
        runtime = device.get("runtime", {})
        validate_cartridge_contract(data, runtime=runtime, context="USB upload")
        chunk_size = min(int(args.chunk_size), int(device.get("max_chunk", args.chunk_size)))
        shown = [-1]

        def report(received: int, total: int) -> None:
            percent = received * 100 // total
            if percent // 10 != shown[0] // 10 or received == total:
                shown[0] = percent
                log_info(f"Uploaded {received}/{total} bytes ({percent}%)")

        result = upload_cartridge(
            client,
            args.cartridge,
            data,
            args.slot,
            command=command,
            chunk_size=chunk_size,
            progress=report,
        )
    finally:
        client.close()
    state = "running" if result.get("running") else "stored"
    log_ok(
        f"'{args.cartridge}' uploaded over USB to {args.slot} "
        f"({result.get('name', 'cartridge')}, {state})"
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
    if getattr(args, "port", None):
        client = ProtocolClient(open_stream(args.port, int(args.baud), float(args.timeout)), float(args.timeout))
        try:
            client.request_json(DEBUG if getattr(args, "debug", False) else RUN, {"slot": args.slot})
        finally:
            client.close()
        log_ok(f"Running {args.slot} through the PRG32 device protocol")
        return
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
