#!/usr/bin/env python3
"""Build and upload PRG32 cartridge games.

The cartridge workflow keeps the PRG32 firmware resident on the board. A game
is linked for the firmware's cartridge RAM address and PRG32 API import table,
packed as a .prg32 file, then uploaded over HTTP or staged into QEMU flash.
"""

from __future__ import annotations

import argparse
import binascii
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from prg32_cartridge_format import (  # noqa: E402
    ARCHITECTURE_PROFILES,
    build_from_files,
    parse_file,
    summary_dict,
)

CART_HEADER = struct.Struct("<4sHHHHIIIIIII32s")
CART_MAGIC = b"PRG2"
CART_ABI_MAJOR = 1
CART_ABI_MINOR = 0
PRG32_CART_FLAG_AUDIO_BLOCK = 1 << 0
PRG32_CART_FLAG_MULTIPLAYER = 1 << 1
AUDIO_BLOCK_MAGIC = b"AUD0"
DEFAULT_PARTITION_TABLE = ROOT / "partitions_prg32.csv"
DEFAULT_CART_SLOT = "cart0"
FALLBACK_CART_RAM_SIZE = 64 * 1024
FALLBACK_CART_MAX_SIZE = 32 * 1024
STORE_DISCOVERY_ABI = "prg32-store-discovery-1.0"
STORE_METADATA_ABI = "prg32-metadata-1.0"
STORE_CONFIG = Path.home() / ".prg32" / "config.json"

IMPORT_NAMES = [
    "prg32_ticks_ms",
    "prg32_input_read",
    "prg32_input_read_player",
    "prg32_input_read_menu",
    "prg32_controller_read",
    "prg32_audio_beep",
    "prg32_audio_tone",
    "prg32_audio_note",
    "prg32_audio_play_notes",
    "prg32_audio_sample_u8",
    "prg32_audio_init",
    "prg32_audio_shutdown",
    "prg32_audio_get_mode",
    "prg32_audio_play_sample",
    "prg32_audio_play_sample_pan",
    "prg32_audio_stop_channel",
    "prg32_audio_stop_all",
    "prg32_audio_note_on",
    "prg32_audio_note_on_pan",
    "prg32_audio_note_off",
    "prg32_audio_play_track",
    "prg32_audio_stop_track",
    "prg32_audio_set_tempo",
    "prg32_audio_set_master_volume",
    "prg32_audio_set_channel_volume",
    "prg32_audio_set_channel_pan",
    "prg32_wifi_start_mode",
    "prg32_wifi_current_mode",
    "prg32_wifi_current_ip",
    "prg32_wifi_current_ssid",
    "prg32_wifi_setup_requested",
    "prg32_wifi_setup_run",
    "prg32_multiplayer_init",
    "prg32_multiplayer_available",
    "prg32_multiplayer_join",
    "prg32_multiplayer_leave",
    "prg32_multiplayer_tick",
    "prg32_multiplayer_set_local_state",
    "prg32_multiplayer_set_input",
    "prg32_multiplayer_get_peer_count",
    "prg32_multiplayer_get_peer",
    "prg32_cart_stored_count",
    "prg32_cart_get_slot_info",
    "prg32_cart_select_slot",
    "prg32_cart_default_slot",
    "prg32_cart_set_default_slot",
    "prg32_cart_select_default",
    "prg32_console_clear",
    "prg32_console_putc",
    "prg32_console_write",
    "prg32_console_hex32",
    "prg32_gfx_clear",
    "prg32_gfx_present",
    "prg32_gfx_set_fullscreen",
    "prg32_gfx_fullscreen_enabled",
    "prg32_gfx_pixel",
    "prg32_gfx_rect",
    "prg32_gfx_text8",
    "prg32_gfx_snapshot_row_rgb565",
    "prg32_band_set_mode",
    "prg32_band_mode",
    "prg32_band_mode_name",
    "prg32_band_set_text",
    "prg32_band_set_game_info",
    "prg32_band_log",
    "prg32_band_set_colors",
    "prg32_band_use_default_colors",
    "prg32_band_load_config",
    "prg32_band_save_config",
    "prg32_splash_draw",
    "prg32_splash_show",
    "prg32_splash_draw_game",
    "prg32_splash_show_game",
    "prg32_splash_show_default",
    "prg32_debug_overlay_draw",
    "prg32_input_wait_released",
    "prg32_keyboard_init",
    "prg32_keyboard_update",
    "prg32_keyboard_draw",
    "prg32_text_input",
    "prg32_tile_clear",
    "prg32_tile_define",
    "prg32_tile_put",
    "prg32_tile_present",
    "prg32_playfield_clear",
    "prg32_playfield_put",
    "prg32_playfield_get",
    "prg32_playfield_scroll",
    "prg32_playfield_scroll_by",
    "prg32_playfield_parallax",
    "prg32_playfield_camera",
    "prg32_playfield_camera_x",
    "prg32_playfield_camera_y",
    "prg32_playfield_draw",
    "prg32_playfield_draw_dual",
    "prg32_playfield_present",
    "prg32_platform_tile_flags",
    "prg32_platform_tile_flags_get",
    "prg32_platform_tile_at",
    "prg32_platform_solid_at",
    "prg32_platform_actor_init",
    "prg32_platform_actor_move",
    "prg32_platform_actor_step",
    "prg32_platform_camera_follow",
    "prg32_sprite_hitbox",
    "prg32_sprite_draw_8x8",
    "prg32_sprite_draw_16x16",
    "prg32_sprite_anim_frame",
    "prg32_sprite_draw_frame",
    "prg32_sprite_anim_init",
    "prg32_sprite_anim_update",
    "prg32_sprite_anim_draw",
    "prg32_score_submit",
]


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


def fetch_runtime(url: str) -> dict:
    endpoint = url.rstrip("/") + "/api/runtime"
    try:
        with urllib.request.urlopen(endpoint, timeout=10) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        raise SystemExit(f"failed to read runtime from {endpoint}: {exc}") from exc


def read_store_config() -> dict:
    try:
        return json.loads(STORE_CONFIG.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"failed to read {STORE_CONFIG}: {exc}") from exc


def store_url(args: argparse.Namespace) -> str:
    value = getattr(args, "store_url", None) or read_store_config().get("store_url")
    if not value:
        raise SystemExit("missing --store-url and no store_url in ~/.prg32/config.json")
    return str(value).rstrip("/")


def store_token(args: argparse.Namespace) -> str | None:
    return getattr(args, "token", None) or read_store_config().get("store_token")


def json_request(url: str, timeout: int = 15) -> dict:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"HTTP {exc.code}: {body}") from exc
    except (urllib.error.URLError, json.JSONDecodeError) as exc:
        raise SystemExit(f"request failed: {exc}") from exc


def catalog_items(body) -> list[dict]:
    if isinstance(body, list):
        return [item for item in body if isinstance(item, dict)]
    if isinstance(body, dict):
        for key in ("games", "items", "cartridges"):
            value = body.get(key)
            if isinstance(value, list):
                return [item for item in value if isinstance(item, dict)]
    return []


def multipart_request(url: str,
                      fields: dict[str, str],
                      files: dict[str, tuple[str, bytes, str]],
                      token: str | None = None) -> urllib.request.Request:
    boundary = "----prg32store" + binascii.hexlify(os.urandom(8)).decode("ascii")
    chunks: list[bytes] = []
    for name, value in fields.items():
        chunks.append(f"--{boundary}\r\n".encode("ascii"))
        chunks.append(
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode("ascii")
        )
        chunks.append(str(value).encode("utf-8") + b"\r\n")
    for name, (filename, data, content_type) in files.items():
        chunks.append(f"--{boundary}\r\n".encode("ascii"))
        chunks.append(
            (
                f'Content-Disposition: form-data; name="{name}"; '
                f'filename="{filename}"\r\n'
                f"Content-Type: {content_type}\r\n\r\n"
            ).encode("ascii")
        )
        chunks.append(data + b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode("ascii"))
    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return urllib.request.Request(url, data=b"".join(chunks), headers=headers, method="POST")


def post_multipart(url: str,
                   fields: dict[str, str],
                   files: dict[str, tuple[str, bytes, str]],
                   token: str | None = None) -> dict:
    request = multipart_request(url, fields, files, token)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            text = response.read().decode("utf-8", "replace")
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                return {"status": response.status, "body": text}
    except urllib.error.HTTPError as exc:
        text = exc.read().decode("utf-8", "replace")
        try:
            body = json.loads(text)
            message = body.get("error", text)
        except json.JSONDecodeError:
            message = text
        raise SystemExit(f"publish failed: HTTP {exc.code}: {message}") from exc


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


def parse_partition_size(token: str) -> int:
    text = token.strip().lower()
    if not text:
        raise ValueError("empty partition size")
    mult = 1
    if text.endswith("k"):
        mult = 1024
        text = text[:-1]
    elif text.endswith("m"):
        mult = 1024 * 1024
        text = text[:-1]
    base = 16 if text.startswith("0x") else 10
    return int(text, base) * mult


def read_partition_slot(path: Path, slot: str) -> tuple[int, int]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise SystemExit(f"failed to read partition table {path}: {exc}") from exc

    for raw in lines:
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            continue
        if cols[0] != slot:
            continue
        try:
            offset = parse_partition_size(cols[3])
            size = parse_partition_size(cols[4])
        except ValueError as exc:
            raise SystemExit(
                f"invalid partition values for {slot} in {path}: {exc}"
            ) from exc
        return (offset, size)

    raise SystemExit(f"partition slot '{slot}' not found in {path}")


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
        "cart_max_size": FALLBACK_CART_MAX_SIZE,
        "cart_ram_size": ram_size,
        "imports": {name: nm[name] for name in IMPORT_NAMES},
    }


def write_imports(path: Path, imports: dict[str, int]) -> None:
    lines = ["/* Generated by tools/prg32_game.py. */"]
    for name in IMPORT_NAMES:
        if name not in imports:
            raise SystemExit(f"runtime import missing: {name}")
        lines.append(f"PROVIDE({name} = 0x{int(imports[name]):08x});")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def ensure_cart_max_size(data: bytes, max_size: int = FALLBACK_CART_MAX_SIZE) -> None:
    if len(data) > max_size:
        raise SystemExit(f"cartridge image is {len(data)} bytes, max is {max_size}")


def write_linker(path: Path, load_addr: int, init_symbol: str) -> None:
    path.write_text(
        f"""/* Generated by tools/prg32_game.py. */
OUTPUT_ARCH(riscv)
ENTRY({init_symbol})

SECTIONS
{{
    . = 0x{load_addr:08x};
    __cart_start = .;

    .text : {{
        *(.text*)
    }}

    .rodata : {{
        *(.rodata*)
    }}

    . = ALIGN(4);
    .data : {{
        *(.data*)
        *(.sdata*)
    }}

    . = ALIGN(4);
    __cart_load_end = .;

    .bss (NOLOAD) : {{
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    }}

    . = ALIGN(4);
    __cart_end = .;

    /DISCARD/ : {{
        *(.comment*)
        *(.riscv.attributes*)
        *(.note*)
    }}
}}
""",
        encoding="utf-8",
    )


def detect_entries(symbols: dict[str, int], prefix: str | None) -> tuple[str, str, str]:
    if prefix:
        entries = (f"{prefix}_init", f"{prefix}_update", f"{prefix}_draw")
        missing = [name for name in entries if name not in symbols]
        if missing:
            raise SystemExit("game ELF is missing entries: " + ", ".join(missing))
        return entries

    def one_suffix(suffix: str) -> str:
        matches = sorted(
            name for name in symbols
            if name.endswith(suffix) and not name.startswith("__cart_")
        )
        if len(matches) != 1:
            raise SystemExit(
                f"could not infer a unique {suffix} entry; pass --entry-prefix"
            )
        return matches[0]

    return (one_suffix("_init"), one_suffix("_update"), one_suffix("_draw"))


def build(args: argparse.Namespace) -> None:
    if args.runtime_url:
        runtime = fetch_runtime(args.runtime_url)
    elif args.firmware_elf:
        runtime = runtime_from_elf(Path(args.firmware_elf), args.tool_prefix)
    else:
        raise SystemExit("build requires --runtime-url or --firmware-elf")

    load_addr = int(runtime["cart_load_addr"])
    if "cart_ram_size" not in runtime:
        print(
            "warning: runtime did not report cart_ram_size; "
            f"using fallback {FALLBACK_CART_RAM_SIZE} bytes",
            file=sys.stderr,
        )
    ram_size = int(runtime.get("cart_ram_size", FALLBACK_CART_RAM_SIZE))
    max_size = int(runtime.get("cart_max_size", FALLBACK_CART_MAX_SIZE))
    imports = {k: int(v) for k, v in runtime["imports"].items()}

    source = Path(args.source)
    out = Path(args.out)
    name = args.name or source.parent.parent.name or source.stem
    name_bytes = name.encode("ascii", "replace")[:31]

    with tempfile.TemporaryDirectory(prefix="prg32-cart-") as tmp_s:
        tmp = Path(tmp_s)
        obj = tmp / "game.o"
        elf = tmp / "game.elf"
        raw = tmp / "game.bin"
        imports_ld = tmp / "imports.ld"
        linker_ld = tmp / "cart.ld"

        compile_cmd = [
            args.tool_prefix + "gcc",
            "-march=" + args.march,
            "-mabi=" + args.mabi,
            "-I", str(ROOT / "components/prg32/include"),
            "-I", str(ROOT / "components/prg32_audio/include"),
            "-I", str(ROOT / "main"),
            "-c", str(source),
            "-o", str(obj),
        ]
        if source.suffix.lower() == ".c":
            compile_cmd[1:1] = [
                "-std=c99",
                "-ffreestanding",
                "-fno-builtin",
                "-Os",
            ]
        else:
            compile_cmd[1:1] = ["-x", "assembler-with-cpp"]

        # Compile once so nm can infer the entry prefix before final linking.
        run(compile_cmd)
        obj_symbols = parse_nm(run([args.tool_prefix + "nm", "--defined-only", str(obj)]))
        init_sym, update_sym, draw_sym = detect_entries(obj_symbols, args.entry_prefix)

        write_imports(imports_ld, imports)
        write_linker(linker_ld, load_addr, init_sym)
        run([
            args.tool_prefix + "gcc",
            "-nostdlib",
            "-march=" + args.march,
            "-mabi=" + args.mabi,
            "-Wl,--no-relax",
            "-Wl,-T," + str(linker_ld),
            "-Wl,-T," + str(imports_ld),
            "-Wl,-Map," + str(tmp / "game.map"),
            str(obj),
            "-o", str(elf),
        ])
        linked_symbols = parse_nm(run([args.tool_prefix + "nm", "--defined-only", str(elf)]))
        run([args.tool_prefix + "objcopy", "-O", "binary", str(elf), str(raw)])

        code = raw.read_bytes()
        audio_block = b""
        flags = 0
        if args.audio_block:
            audio_block = Path(args.audio_block).read_bytes()
            if len(audio_block) < 40 or audio_block[:4] != AUDIO_BLOCK_MAGIC:
                raise SystemExit(
                    f"{args.audio_block} is not a PRG32 AUDIO block"
                )
            block_size = struct.unpack_from("<I", audio_block, 36)[0]
            if block_size != len(audio_block):
                raise SystemExit(
                    f"{args.audio_block} declares {block_size} bytes "
                    f"but file has {len(audio_block)}"
                )
            flags |= PRG32_CART_FLAG_AUDIO_BLOCK
        if args.multiplayer:
            flags |= PRG32_CART_FLAG_MULTIPLAYER
        start = linked_symbols["__cart_start"]
        end = linked_symbols["__cart_end"]
        mem_size = end - start
        if mem_size <= 0 or mem_size > ram_size:
            raise SystemExit(f"cartridge needs {mem_size} bytes, runtime has {ram_size}")
        if len(code) > mem_size:
            raise SystemExit("internal error: binary is larger than cartridge memory")

        entries = {
            "init": linked_symbols[init_sym] - load_addr,
            "update": linked_symbols[update_sym] - load_addr,
            "draw": linked_symbols[draw_sym] - load_addr,
        }
        crc = binascii.crc32(code) & 0xffffffff
        header = CART_HEADER.pack(
            CART_MAGIC,
            CART_ABI_MAJOR,
            CART_ABI_MINOR,
            CART_HEADER.size,
            flags,
            load_addr,
            len(code),
            mem_size,
            entries["init"],
            entries["update"],
            entries["draw"],
            crc,
            name_bytes + b"\0" * (32 - len(name_bytes)),
        )
        image = header + code + audio_block
        if len(image) > max_size:
            raise SystemExit(f"cartridge image is {len(image)} bytes, max is {max_size}")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(image)

    print(
        f"built {out} name={name} load=0x{load_addr:08x} "
        f"code={len(code)} mem={mem_size} audio={len(audio_block)}"
    )


def upload(args: argparse.Namespace) -> None:
    data = Path(args.cartridge).read_bytes()
    ensure_cart_max_size(data)
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


def store_discover(args: argparse.Namespace) -> None:
    try:
        from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
    except Exception as exc:
        print("zeroconf not installed. Run: pip install zeroconf")
        raise SystemExit(1) from exc

    class Listener(ServiceListener):
        def __init__(self) -> None:
            self.found: list[tuple[str, str]] = []

        def add_service(self, zc, service_type, name):
            info = zc.get_service_info(service_type, name)
            if not info or not info.addresses:
                return
            address = ".".join(str(part) for part in info.addresses[0])
            url = f"http://{address}:{info.port}"
            self.found.append((name.split("._", 1)[0].rstrip("."), url))

        def update_service(self, zc, service_type, name):
            self.add_service(zc, service_type, name)

        def remove_service(self, zc, service_type, name):
            return None

    zc = Zeroconf()
    listener = Listener()
    ServiceBrowser(zc, "_prg32store._tcp.local.", listener)
    try:
        import time
        time.sleep(args.timeout)
    finally:
        zc.close()
    for name, url in listener.found:
        abi = ""
        try:
            body = json_request(url + "/.well-known/prg32-store.json", timeout=3)
            abi = body.get("abi", "")
            name = body.get("name", name)
        except SystemExit:
            pass
        print(f"Found: {name}")
        print(f"  URL: {url}")
        print(f"  ABI: {abi or STORE_DISCOVERY_ABI}")


def store_list(args: argparse.Namespace) -> None:
    body = json_request(store_url(args) + "/api/games")
    rows = catalog_items(body)
    print(f"{'ID':32} {'Title':24} {'Version':8} Architectures")
    for item in rows:
        archs = item.get("architectures", [])
        if args.architecture and args.architecture not in archs:
            continue
        if isinstance(archs, list):
            arch_text = ", ".join(str(a) for a in archs)
        else:
            arch_text = str(archs)
        print(
            f"{str(item.get('id', ''))[:32]:32} "
            f"{str(item.get('title', ''))[:24]:24} "
            f"{str(item.get('version', ''))[:8]:8} {arch_text}"
        )


def store_download(args: argparse.Namespace) -> None:
    query = {"architecture": args.architecture}
    if args.version:
        query["version"] = args.version
    endpoint = (
        store_url(args)
        + "/api/games/"
        + urllib.parse.quote(args.game_id, safe="")
        + "/download?"
        + urllib.parse.urlencode(query)
    )
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(endpoint, timeout=60) as response, out.open("wb") as f:
            total = int(response.headers.get("Content-Length", "0") or "0")
            done = 0
            while True:
                chunk = response.read(64 * 1024)
                if not chunk:
                    break
                f.write(chunk)
                done += len(chunk)
                if total:
                    print(f"\r{done * 100 // total:3d}% {done}/{total} bytes", end="")
            if total:
                print()
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"download failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"download failed: {exc}") from exc
    print(f"saved {out}")


def infer_architecture(firmware_elf: str | None) -> str:
    if firmware_elf:
        sdkconfig = Path(firmware_elf).with_name("sdkconfig")
        if sdkconfig.exists():
            text = sdkconfig.read_text(encoding="utf-8", errors="replace")
            if "CONFIG_PRG32_DISPLAY_QEMU_RGB=y" in text:
                return "qemu"
    return "esp32c6"


def make_metadata(args: argparse.Namespace, architecture: str, cartridge_file: str) -> dict:
    game_id = args.id or f"org.prg32.{args.name}"
    tags = [tag.strip() for tag in (args.tags or "").split(",") if tag.strip()]
    return {
        "abi": STORE_METADATA_ABI,
        "id": game_id,
        "title": args.name,
        "version": args.version,
        "summary": args.summary or "",
        "tags": tags,
        "assets": {
            "icon": Path(args.icon).name if args.icon else "",
            "splash": Path(args.splash).name if args.splash else "",
        },
        "architectures": [{"id": architecture, "file": cartridge_file}],
    }


def publish(args: argparse.Namespace) -> None:
    architecture = args.architecture or infer_architecture(args.firmware_elf)
    with tempfile.TemporaryDirectory(prefix="prg32-publish-") as tmp_s:
        tmp = Path(tmp_s)
        cart = tmp / f"{args.name}-{architecture}.prg32"
        build_args = argparse.Namespace(**vars(args))
        build_args.out = str(cart)
        build_args.runtime_url = None
        build_args.audio_block = None
        build_args.multiplayer = False
        build_args.march = "rv32imc_zicsr_zifencei"
        build_args.mabi = "ilp32"
        build(build_args)
        manifest = tmp / "manifest.json"
        metadata = make_metadata(args, architecture, cart.name)
        manifest.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        bundle = tmp / f"{args.name}.zip"
        with zipfile.ZipFile(bundle, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.write(manifest, "manifest.json")
            zf.write(cart, cart.name)
            if args.icon:
                zf.write(args.icon, Path(args.icon).name)
            if args.splash:
                zf.write(args.splash, Path(args.splash).name)
            if args.colophon:
                zf.write(args.colophon, Path(args.colophon).name)
        response = post_multipart(
            store_url(args) + "/api/publish/bundle",
            {},
            {"bundle": (bundle.name, bundle.read_bytes(), "application/zip")},
            store_token(args),
        )
    print(json.dumps(response, indent=2, sort_keys=True))
    if response.get("status") == "pending" or response.get("review_required"):
        print("Submitted for review")
    else:
        print("Published")


def pack_bundle(args: argparse.Namespace) -> None:
    manifest = Path(args.manifest)
    data = json.loads(manifest.read_text(encoding="utf-8"))
    base = manifest.parent
    files = {"manifest.json": manifest}
    assets = data.get("assets", {})
    if isinstance(assets, dict):
        for filename in assets.values():
            if filename:
                files[Path(filename).name] = base / filename
    for arch in data.get("architectures", []):
        filename = arch.get("file") if isinstance(arch, dict) else None
        if filename:
            files[Path(filename).name] = base / filename
    for path in files.values():
        if not path.exists():
            raise SystemExit(f"bundle file not found: {path}")
    out = Path(args.out)
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for arcname, path in files.items():
            zf.write(path, arcname)
    total = 0
    print(f"Bundle: {out}")
    for arcname, path in files.items():
        size = path.stat().st_size
        total += size
        print(f"  {arcname:28} {size / 1024:6.1f} KB")
    print(f"Total: {total / 1024:.1f} KB")


def publish_bundle(args: argparse.Namespace) -> None:
    bundle = Path(args.bundle)
    with bundle.open("rb") as f:
        if f.read(4) != b"PK\x03\x04":
            raise SystemExit(f"{bundle} is not a zip bundle")
    response = post_multipart(
        store_url(args) + "/api/publish/bundle",
        {},
        {"bundle": (bundle.name, bundle.read_bytes(), "application/zip")},
        store_token(args),
    )
    published = response.get("published") or response.get("submitted") or response
    print(json.dumps(published, indent=2, sort_keys=True))
    if response.get("status") == "pending" or response.get("review_required"):
        print("Submitted for review")


def upload_qemu(args: argparse.Namespace) -> None:
    flash = Path(args.flash)
    data = Path(args.cartridge).read_bytes()
    ensure_cart_max_size(data)
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
        f.seek(cart_offset)
        f.write(b"\xff" * cart_size)
        f.seek(cart_offset)
        f.write(data)
    print(
        f"staged {args.cartridge} into {flash} at {args.slot} "
        f"(offset=0x{cart_offset:06x}, size={cart_size})"
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


def doctor(args: argparse.Namespace) -> None:
    failures = 0

    def ok(msg: str) -> None:
        print(f"[OK] {msg}")

    def fail(msg: str) -> None:
        nonlocal failures
        failures += 1
        print(f"[FAIL] {msg}")

    if args.host_only:
        ok("host-only mode skips ESP-IDF toolchain checks")
    else:
        if shutil.which("idf.py"):
            ok("idf.py found")
        else:
            fail("idf.py missing (source ESP-IDF export.sh)")

        gcc_name = args.tool_prefix + "gcc"
        if shutil.which(gcc_name):
            ok(f"{gcc_name} found")
        else:
            fail(f"{gcc_name} missing (source ESP-IDF export.sh)")

    partitions_path = Path(args.partitions)
    if partitions_path.exists():
        ok(f"partitions file found: {partitions_path}")
        try:
            offset, size = read_partition_slot(partitions_path, args.slot)
        except SystemExit as exc:
            fail(str(exc))
        else:
            ok(
                f"partition {args.slot} parsed "
                f"(offset=0x{offset:06x}, size={size})"
            )
    else:
        fail(f"partitions file missing: {partitions_path}")

    if failures:
        raise SystemExit(1)


def attach_metadata(args: argparse.Namespace) -> None:
    build_from_files(
        args.cartridge,
        metadata_path=args.metadata,
        icon_path=args.icon,
        out_path=args.out,
        screenshot_path=args.screenshot,
        signature_path=args.signature,
        colophon_path=args.colophon,
        architecture=args.architecture,
    )
    parsed = parse_file(args.out)
    arch = ""
    if parsed.metadata:
        runtime = parsed.metadata.get("runtime", {})
        if isinstance(runtime, dict) and runtime.get("architecture"):
            arch = f" architecture={runtime['architecture']}"
    print(
        f"wrote {args.out} legacy={len(parsed.legacy_payload)} "
        f"blocks={len(parsed.blocks)}{arch}"
    )


def inspect_metadata(args: argparse.Namespace) -> None:
    parsed = parse_file(args.cartridge)
    print(json.dumps(summary_dict(parsed), indent=2, sort_keys=True))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-prefix", default="riscv32-esp-elf-")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("runtime", help="print runtime linker information")
    p.add_argument("--url")
    p.add_argument("--firmware-elf")
    p.set_defaults(func=runtime)

    p = sub.add_parser("build", help="build a .prg32 cartridge from assembly or C")
    p.add_argument("source")
    p.add_argument("--out", required=True)
    p.add_argument("--name")
    p.add_argument("--entry-prefix")
    p.add_argument("--runtime-url")
    p.add_argument("--firmware-elf")
    p.add_argument(
        "--audio-block",
        help="optional PRG32 AUDIO block produced by tools/prg32audio_pack.py",
    )
    p.add_argument(
        "--multiplayer",
        action="store_true",
        help="mark the cartridge as using the PRG32 multiplayer service",
    )
    p.add_argument("--march", default="rv32imc_zicsr_zifencei")
    p.add_argument("--mabi", default="ilp32")
    p.set_defaults(func=build)

    p = sub.add_parser("upload", help="upload a cartridge to hardware over HTTP")
    p.add_argument("cartridge")
    p.add_argument("--url", "--store-url", dest="url", default="http://192.168.4.1")
    p.add_argument("--slot", default=DEFAULT_CART_SLOT)
    p.set_defaults(func=upload)

    p = sub.add_parser("upload-qemu", help="stage a cartridge into QEMU flash")
    p.add_argument("cartridge")
    p.add_argument("--flash", default="build-qemu/flash_image.bin")
    p.add_argument("--partitions", default=str(DEFAULT_PARTITION_TABLE))
    p.add_argument("--slot", default=DEFAULT_CART_SLOT)
    p.set_defaults(func=upload_qemu)

    p = sub.add_parser("doctor", help="check local toolchain prerequisites")
    p.add_argument("--partitions", default=str(DEFAULT_PARTITION_TABLE))
    p.add_argument("--slot", default=DEFAULT_CART_SLOT)
    p.add_argument(
        "--host-only",
        action="store_true",
        help="skip ESP-IDF and RISC-V toolchain checks for CI/unit-test hosts",
    )
    p.set_defaults(func=doctor)

    p = sub.add_parser(
        "attach-metadata",
        help="append or replace a PRG32META metadata trailer",
    )
    p.add_argument("cartridge")
    p.add_argument("--out", required=True)
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

    p = sub.add_parser(
        "inspect-metadata",
        help="print the PRG32META trailer summary for a cartridge",
    )
    p.add_argument("cartridge")
    p.set_defaults(func=inspect_metadata)

    p = sub.add_parser("store-discover", help="find CartridgeStore instances with mDNS")
    p.add_argument("--timeout", type=float, default=5)
    p.set_defaults(func=store_discover)

    p = sub.add_parser("store-list", help="list cartridges from a CartridgeStore")
    p.add_argument("--store-url")
    p.add_argument("--architecture", choices=sorted(ARCHITECTURE_PROFILES))
    p.set_defaults(func=store_list)

    p = sub.add_parser("store-download", help="download a cartridge from a CartridgeStore")
    p.add_argument("game_id")
    p.add_argument("--store-url")
    p.add_argument("--architecture", required=True, choices=sorted(ARCHITECTURE_PROFILES))
    p.add_argument("--version")
    p.add_argument("--out", required=True)
    p.set_defaults(func=store_download)

    p = sub.add_parser("publish", help="build and publish a cartridge bundle")
    p.add_argument("source")
    p.add_argument("--firmware-elf", required=True)
    p.add_argument("--entry-prefix", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--store-url")
    p.add_argument("--architecture", choices=sorted(ARCHITECTURE_PROFILES))
    p.add_argument("--id")
    p.add_argument("--version", default="1.0.0")
    p.add_argument("--summary")
    p.add_argument("--tags")
    p.add_argument("--icon")
    p.add_argument("--splash")
    p.add_argument("--colophon")
    p.add_argument("--token")
    p.set_defaults(func=publish)

    p = sub.add_parser("pack-bundle", help="pack a flat CartridgeStore zip bundle")
    p.add_argument("--manifest", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=pack_bundle)

    p = sub.add_parser("publish-bundle", help="publish a CartridgeStore zip bundle")
    p.add_argument("bundle")
    p.add_argument("--store-url")
    p.add_argument("--token")
    p.set_defaults(func=publish_bundle)

    args = parser.parse_args(argv)
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
