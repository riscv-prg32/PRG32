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
from prg32.utilities.runtime_handler import fetch_runtime, validate_cartridge_contract, ensure_cart_max_size

def upload_esp32c6(args: argparse.Namespace) -> None:
    data = Path(args.cartridge).read_bytes()
    ensure_cart_max_size(data)
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
