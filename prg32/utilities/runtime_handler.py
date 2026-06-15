from __future__ import annotations
import argparse
import subprocess
import sys
from pathlib import Path
from prg32.utilities.env_variables import *
from prg32.abi.abi_generated import ABI_HASH, FEATURE_BITS
import urllib.error
import urllib.request
import json

def run(cmd: list[str], cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError as exc:
        raise SystemExit(f"missing tool: {cmd[0]}") from exc
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stdout)
        sys.stderr.write(exc.stderr)
        raise SystemExit(f"command failed: {' '.join(cmd)}") from exc
    return result.stdout

def parse_nm(text: str) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                symbols[parts[2]] = int(parts[0], 16)
            except ValueError:
                continue
    return symbols

def parse_nm_sizes(text: str) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 4:
            name = parts[-1]
            try:
                size = int(parts[1], 16)
            except ValueError:
                continue
            symbols[name] = size
    return symbols

def runtime_from_elf(path: Path, tool_prefix: str) -> dict:
    nm = parse_nm(run([tool_prefix + "nm", "-g", "--defined-only", str(path)]))
    if "prg32_cart_exec" not in nm:
        raise SystemExit("firmware ELF does not export prg32_cart_exec")
    size_nm = parse_nm_sizes(
        run([tool_prefix + "nm", "-S", "-g", "--defined-only", str(path)])
    )
    missing = [name for name in IMPORT_NAMES if name not in nm]
    if missing:
        raise SystemExit("firmware ELF is missing imports: " + ", ".join(missing))
    ram_size = size_nm.get("prg32_cart_exec")
    if not ram_size:
        ram_size = FALLBACK_CART_RAM_SIZE
        print(
            "warning: could not infer prg32_cart_exec size from ELF symbols; "
            f"using fallback cart RAM size {ram_size} bytes",
            file=sys.stderr,
        )
    return {
        "cart_load_addr": nm["prg32_cart_exec"],
        "cart_ram_size": ram_size,
        "imports": {name: nm[name] for name in IMPORT_NAMES},
    }

def fetch_runtime(url: str) -> dict:
    endpoint = url.rstrip("/") + "/api/runtime"
    try:
        with urllib.request.urlopen(endpoint, timeout=10) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        raise SystemExit(f"failed to read runtime from {endpoint}: {exc}") from exc

def ensure_cart_max_size(data: bytes) -> None:
    if len(data) > 64 * 1024:
        raise SystemExit(
            f"cartridge is {len(data)} bytes, which exceeds the PRG32 hardware "
            "limit of 64KB (65536 bytes)."
        )

def runtime(args: argparse.Namespace) -> None:
    if args.url:
        print(json.dumps(fetch_runtime(args.url), indent=2, sort_keys=True))
    elif args.firmware_elf:
        print(json.dumps(runtime_from_elf(Path(args.firmware_elf), args.tool_prefix),
                         indent=2,
                         sort_keys=True))
    else:
        raise SystemExit("runtime requires --url or --firmware-elf")

def cartridge_contract(data: bytes) -> dict:
    if len(data) < CART_HEADER.size:
        raise SystemExit("cartridge is too small to contain a PRG32 header")
    fields = CART_HEADER.unpack_from(data, 0)
    if fields[0] != CART_MAGIC:
        raise SystemExit("cartridge is not a PRG32 .prg32 image")
    header = {
        "abi_major": fields[1],
        "abi_minor": fields[2],
        "header_size": fields[3],
        "flags": fields[4],
        "load_addr": fields[5],
        "code_size": fields[6],
        "mem_size": fields[7],
        "import_model": PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE,
        "abi_hash": None,
        "required_features": 0,
    }
    if header["header_size"] >= CART_HEADER_V2.size:
        if len(data) < CART_HEADER_V2.size:
            raise SystemExit("cartridge v2 header is truncated")
        v2 = CART_HEADER_V2.unpack_from(data, 0)
        header.update({
            "abi_hash": v2[13],
            "required_features": v2[14],
            "import_model": v2[19],
        })
    return header

def validate_cartridge_contract(
    data: bytes,
    *,
    runtime: dict | None = None,
    context: str = "cartridge",
) -> None:
    h = cartridge_contract(data)
    expected_major = int((runtime or {}).get("cart_abi_major", CART_ABI_MAJOR))
    expected_hash = int((runtime or {}).get("cart_abi_hash", ABI_HASH))
    default_features = 0
    for bit in FEATURE_BITS.values():
        default_features |= int(bit)
    provided_features = int((runtime or {}).get("cart_abi_features", default_features))
    
    # In prg32.utilities.env_variables, we have FALLBACK_CART_LOAD_ADDR
    load_addr = int((runtime or {}).get("cart_load_addr", getattr(sys.modules['prg32.utilities.env_variables'], 'FALLBACK_CART_LOAD_ADDR', 0x40800000)))
    
    if h["abi_major"] != expected_major:
        raise SystemExit(
            f"{context} rejected: cartridge ABI major {h['abi_major']} "
            f"is not compatible with runtime ABI major {expected_major}"
        )
    if h["import_model"] == PRG32_IMPORT_MODEL_ABI_TABLE:
        if h["abi_hash"] != expected_hash:
            raise SystemExit(
                f"{context} rejected: portable ABI hash "
                f"0x{int(h['abi_hash'] or 0):08x} does not match runtime "
                f"0x{expected_hash:08x}; rebuild the cartridge with this PRG32 checkout"
            )
        missing = int(h["required_features"]) & ~provided_features
        if missing:
            raise SystemExit(
                f"{context} rejected: cartridge requires unsupported ABI "
                f"feature bits 0x{missing:08x}"
            )
        return
    if h["import_model"] != PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE:
        raise SystemExit(
            f"{context} rejected: unsupported cartridge import model {h['import_model']}"
        )
    if h["load_addr"] != load_addr:
        raise SystemExit(
            f"{context} rejected: legacy cartridge was linked for "
            f"0x{h['load_addr']:08x}, but this runtime loads cartridges at "
            f"0x{load_addr:08x}; rebuild with --portable or against this firmware"
        )