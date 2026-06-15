import json
import urllib.request
import urllib.error
import urllib.parse
import os
import binascii
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
STORE_CONFIG = Path.home() / ".prg32" / "config.json"
STORE_METADATA_ABI = "prg32-metadata-1.0"
STORE_DISCOVERY_ABI = "prg32-store-discovery-1.0"

from prg32.store.cartridge_format import ARCHITECTURE_PROFILES, build_from_files, parse_file, summary_dict

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

def infer_architecture(firmware_elf: str | None) -> str:
    if firmware_elf:
        sdkconfig = Path(firmware_elf).with_name("sdkconfig")
        if sdkconfig.exists():
            text = sdkconfig.read_text(encoding="utf-8", errors="replace")
            if "CONFIG_PRG32_DISPLAY_QEMU_RGB=y" in text:
                return "qemu"
    return "esp32c6"

