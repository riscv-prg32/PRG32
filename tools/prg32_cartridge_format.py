#!/usr/bin/env python3
"""PRG32 cartridge metadata trailer utilities.

The metadata trailer is appended after the legacy cartridge payload:

    PRG2 header + code payload + optional AUD0 block + PRG32META trailer

Old firmware and tools keep using the legacy header sizes and ignore bytes after
the code/AUDIO payload. New tools locate the trailer at that canonical payload
end and decode deterministic TLV blocks.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import struct
import warnings
from typing import Any, Iterable


CART_MAGIC = b"PRG2"
CART_HEADER = struct.Struct("<4sHHHHIIIIIII32s")
PRG32_CART_FLAG_AUDIO_BLOCK = 1 << 0
AUDIO_BLOCK_MAGIC = b"AUD0"
AUDIO_BLOCK_HEADER_SIZE = 40
AUDIO_BLOCK_SIZE_OFFSET = 36

TRAILER_MAGIC = b"PRG32META"
TRAILER_VERSION = 1
TRAILER_HEADER = struct.Struct("<9sBHI")
TLV_HEADER = struct.Struct("<4sI")

METADATA_ABI = "prg32-metadata-1.0"
COLOPHON_ABI = "prg32-colophon-1.0"

BLOCK_META = "META"
BLOCK_ICON = "ICON"
BLOCK_SCREENSHOT = "SCRN"
BLOCK_SIGNATURE = "SIGN"
BLOCK_COLOPHON = "COLO"

KNOWN_BLOCKS = {
    BLOCK_META,
    BLOCK_ICON,
    BLOCK_SCREENSHOT,
    BLOCK_SIGNATURE,
    BLOCK_COLOPHON,
}

ARCHITECTURE_PROFILES: dict[str, dict[str, str]] = {
    "esp32c6": {
        "id": "esp32c6",
        "label": "ESP32-C6 hardware",
        "platform": "PRG32",
        "target": "esp32c6",
        "display": "ili9341",
        "isa": "RV32I",
    },
    "qemu": {
        "id": "qemu",
        "label": "QEMU virtual screen",
        "platform": "PRG32",
        "target": "esp32c3",
        "display": "qemu-rgb",
        "isa": "RV32I",
    },
}


class CartridgeFormatError(ValueError):
    """Raised when a cartridge or trailer is structurally invalid."""


class MetadataValidationError(ValueError):
    """Raised when metadata JSON does not satisfy prg32-metadata-1.0."""


class ColophonValidationError(ValueError):
    """Raised when colophon JSON does not satisfy prg32-colophon-1.0."""


@dataclass(frozen=True)
class TrailerBlock:
    """One raw trailer TLV block."""

    block_type: str
    data: bytes


@dataclass(frozen=True)
class CartridgeImage:
    """Parsed cartridge image with optional decoded metadata trailer blocks."""

    data: bytes
    legacy_payload: bytes
    trailer_present: bool
    blocks: tuple[TrailerBlock, ...]
    metadata: dict[str, Any] | None = None
    icon: bytes | None = None
    screenshot: bytes | None = None
    signature: bytes | None = None
    signature_json: Any | None = None
    colophon: dict[str, Any] | None = None

    @property
    def unknown_blocks(self) -> tuple[TrailerBlock, ...]:
        return tuple(block for block in self.blocks if block.block_type not in KNOWN_BLOCKS)


def _read_json(path: str | Path) -> dict[str, Any]:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CartridgeFormatError(f"{path} is not valid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise CartridgeFormatError(f"{path} must contain a JSON object")
    return data


def deterministic_json_bytes(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def detect_image_mime(data: bytes) -> str | None:
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return "image/png"
    if data.startswith(b"\xff\xd8\xff"):
        return "image/jpeg"
    return None


def normalize_architecture(architecture: str | None) -> str | None:
    if architecture is None:
        return None
    key = architecture.strip().lower()
    if key in {"esp32-c6", "riscv-esp32-c6", "risc-v-esp32-c6", "hardware"}:
        key = "esp32c6"
    if key in {"esp32c3-qemu", "qemu-rgb", "emulator"}:
        key = "qemu"
    if key not in ARCHITECTURE_PROFILES:
        supported = ", ".join(sorted(ARCHITECTURE_PROFILES))
        raise MetadataValidationError(
            f"unsupported architecture '{architecture}', expected one of: {supported}"
        )
    return key


def _merge_architecture(runtime: dict[str, Any], architecture: str | None) -> None:
    runtime.setdefault("platform", "PRG32")
    runtime.setdefault("isa", "RV32I")
    if not architecture:
        return
    profile = dict(ARCHITECTURE_PROFILES[architecture])
    runtime["architecture"] = architecture
    runtime.setdefault("target", profile["target"])
    runtime.setdefault("display", profile["display"])
    existing = runtime.get("architectures")
    if not isinstance(existing, list):
        existing = []
    by_id = {
        item.get("id"): dict(item)
        for item in existing
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    by_id[architecture] = {**profile, **by_id.get(architecture, {})}
    runtime["architectures"] = [by_id[key] for key in sorted(by_id)]


def normalize_metadata(
    metadata: dict[str, Any],
    *,
    architecture: str | None = None,
    has_screenshot: bool = False,
    has_signature: bool = False,
    has_colophon: bool = False,
) -> dict[str, Any]:
    normalized = json.loads(json.dumps(metadata))
    normalized.setdefault("abi", METADATA_ABI)
    runtime = normalized.setdefault("runtime", {})
    if not isinstance(runtime, dict):
        raise MetadataValidationError("metadata.runtime must be an object")
    _merge_architecture(runtime, normalize_architecture(architecture))

    assets = normalized.setdefault("assets", {})
    if not isinstance(assets, dict):
        raise MetadataValidationError("metadata.assets must be an object")
    assets.setdefault("icon", {"block": BLOCK_ICON, "mime": "image/png"})
    if has_screenshot:
        assets.setdefault(
            "screenshot",
            {"block": BLOCK_SCREENSHOT, "mime": "image/png", "optional": True},
        )
    if has_signature:
        normalized.setdefault(
            "signature",
            {"block": BLOCK_SIGNATURE, "optional": True},
        )
    if has_colophon:
        normalized.setdefault(
            "colophon",
            {"block": BLOCK_COLOPHON, "abi": COLOPHON_ABI},
        )
    validate_metadata(normalized)
    return normalized


def validate_metadata(metadata: dict[str, Any]) -> None:
    if metadata.get("abi") != METADATA_ABI:
        raise MetadataValidationError(f"metadata.abi must be {METADATA_ABI}")
    for field in ("id", "title", "version"):
        if not isinstance(metadata.get(field), str) or not metadata[field].strip():
            raise MetadataValidationError(f"metadata.{field} is required")
    authors = metadata.get("authors", [])
    if not isinstance(authors, list):
        raise MetadataValidationError("metadata.authors must be an array")
    tags = metadata.get("tags", [])
    if not isinstance(tags, list):
        raise MetadataValidationError("metadata.tags must be an array")
    runtime = metadata.get("runtime", {})
    if not isinstance(runtime, dict):
        raise MetadataValidationError("metadata.runtime must be an object")
    architectures = runtime.get("architectures", [])
    if architectures is not None and not isinstance(architectures, list):
        raise MetadataValidationError("metadata.runtime.architectures must be an array")


def normalize_colophon(colophon: dict[str, Any]) -> dict[str, Any]:
    normalized = json.loads(json.dumps(colophon))
    normalized.setdefault("abi", COLOPHON_ABI)
    normalized.setdefault("authors", [])
    normalized.setdefault("controls", [])
    validate_colophon(normalized)
    return normalized


def validate_colophon(colophon: dict[str, Any]) -> None:
    if colophon.get("abi") != COLOPHON_ABI:
        raise ColophonValidationError(f"colophon.abi must be {COLOPHON_ABI}")
    for field in ("title", "version"):
        if not isinstance(colophon.get(field), str) or not colophon[field].strip():
            raise ColophonValidationError(f"colophon.{field} is required")
    developer = colophon.get("developer")
    if not isinstance(developer, dict):
        raise ColophonValidationError("colophon.developer must be an object")
    name = developer.get("name")
    if not isinstance(name, str) or not name.strip():
        raise ColophonValidationError("colophon.developer.name is required")
    if not isinstance(colophon.get("authors"), list):
        raise ColophonValidationError("colophon.authors must be an array")
    if not isinstance(colophon.get("controls"), list):
        raise ColophonValidationError("colophon.controls must be an array")


def _decode_json_block(data: bytes, name: str) -> dict[str, Any]:
    try:
        parsed = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CartridgeFormatError(f"{name} block is not valid UTF-8 JSON") from exc
    if not isinstance(parsed, dict):
        raise CartridgeFormatError(f"{name} block must contain a JSON object")
    return parsed


def legacy_payload_end(data: bytes) -> int:
    if len(data) < CART_HEADER.size:
        raise CartridgeFormatError("cartridge is smaller than the PRG2 header")
    fields = CART_HEADER.unpack_from(data, 0)
    magic = fields[0]
    header_size = fields[3]
    flags = fields[4]
    code_size = fields[6]
    if magic != CART_MAGIC:
        raise CartridgeFormatError("bad cartridge magic")
    if header_size < CART_HEADER.size or header_size > len(data):
        raise CartridgeFormatError("invalid cartridge header size")
    end = header_size + code_size
    if code_size == 0 or end > len(data):
        raise CartridgeFormatError("truncated cartridge code payload")
    if flags & PRG32_CART_FLAG_AUDIO_BLOCK:
        if end + AUDIO_BLOCK_HEADER_SIZE > len(data):
            raise CartridgeFormatError("truncated cartridge AUDIO block")
        if data[end:end + 4] != AUDIO_BLOCK_MAGIC:
            raise CartridgeFormatError("bad cartridge AUDIO block magic")
        audio_size = struct.unpack_from("<I", data, end + AUDIO_BLOCK_SIZE_OFFSET)[0]
        if audio_size < AUDIO_BLOCK_HEADER_SIZE or end + audio_size > len(data):
            raise CartridgeFormatError("invalid cartridge AUDIO block length")
        end += audio_size
    return end


def parse_cartridge(data: bytes) -> CartridgeImage:
    legacy_end = legacy_payload_end(data)
    remaining = data[legacy_end:]
    if not remaining:
        return CartridgeImage(
            data=data,
            legacy_payload=data,
            trailer_present=False,
            blocks=(),
        )
    if not remaining.startswith(TRAILER_MAGIC):
        return CartridgeImage(
            data=data,
            legacy_payload=data,
            trailer_present=False,
            blocks=(),
        )
    if len(remaining) < TRAILER_HEADER.size:
        raise CartridgeFormatError("truncated metadata trailer header")

    magic, version, entry_count, trailer_size = TRAILER_HEADER.unpack_from(remaining, 0)
    if magic != TRAILER_MAGIC:
        raise CartridgeFormatError("bad metadata trailer magic")
    if version != TRAILER_VERSION:
        raise CartridgeFormatError(
            f"unsupported metadata trailer version {version}"
        )
    if trailer_size < TRAILER_HEADER.size or trailer_size > len(remaining):
        raise CartridgeFormatError("invalid metadata trailer length")
    if trailer_size != len(remaining):
        raise CartridgeFormatError("unexpected bytes after metadata trailer")

    pos = TRAILER_HEADER.size
    blocks: list[TrailerBlock] = []
    for _ in range(entry_count):
        if pos + TLV_HEADER.size > trailer_size:
            raise CartridgeFormatError("truncated metadata TLV header")
        raw_type, length = TLV_HEADER.unpack_from(remaining, pos)
        pos += TLV_HEADER.size
        if length > trailer_size - pos:
            raise CartridgeFormatError("metadata TLV length exceeds trailer")
        try:
            block_type = raw_type.decode("ascii")
        except UnicodeDecodeError as exc:
            raise CartridgeFormatError("metadata TLV type must be ASCII") from exc
        blocks.append(TrailerBlock(block_type, remaining[pos:pos + length]))
        pos += length
    if pos != trailer_size:
        raise CartridgeFormatError("metadata trailer entry count does not match length")

    metadata: dict[str, Any] | None = None
    icon: bytes | None = None
    screenshot: bytes | None = None
    signature: bytes | None = None
    signature_json: Any | None = None
    colophon: dict[str, Any] | None = None
    for block in blocks:
        if block.block_type == BLOCK_META:
            metadata = _decode_json_block(block.data, BLOCK_META)
        elif block.block_type == BLOCK_ICON:
            icon = block.data
        elif block.block_type == BLOCK_SCREENSHOT:
            screenshot = block.data
        elif block.block_type == BLOCK_SIGNATURE:
            signature = block.data
            try:
                signature_json = json.loads(block.data.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                signature_json = None
        elif block.block_type == BLOCK_COLOPHON:
            colophon = _decode_json_block(block.data, BLOCK_COLOPHON)

    if metadata is not None:
        validate_metadata(metadata)
    if colophon is not None:
        validate_colophon(normalize_colophon(colophon))
    return CartridgeImage(
        data=data,
        legacy_payload=data[:legacy_end],
        trailer_present=True,
        blocks=tuple(blocks),
        metadata=metadata,
        icon=icon,
        screenshot=screenshot,
        signature=signature,
        signature_json=signature_json,
        colophon=colophon,
    )


def build_trailer(blocks: Iterable[TrailerBlock]) -> bytes:
    encoded = bytearray()
    block_list = list(blocks)
    for block in block_list:
        raw_type = block.block_type.encode("ascii")
        if len(raw_type) != 4:
            raise CartridgeFormatError("metadata TLV block types must be four ASCII bytes")
        encoded += TLV_HEADER.pack(raw_type, len(block.data))
        encoded += block.data
    trailer_size = TRAILER_HEADER.size + len(encoded)
    return TRAILER_HEADER.pack(
        TRAILER_MAGIC,
        TRAILER_VERSION,
        len(block_list),
        trailer_size,
    ) + bytes(encoded)


def build_cartridge(
    legacy_payload: bytes,
    *,
    metadata: dict[str, Any],
    icon: bytes,
    screenshot: bytes | None = None,
    signature: bytes | dict[str, Any] | None = None,
    colophon: dict[str, Any] | None = None,
    architecture: str | None = None,
    extra_blocks: Iterable[TrailerBlock] = (),
    preserve_unknown: bool = True,
) -> bytes:
    parsed = parse_cartridge(legacy_payload)
    base_payload = parsed.legacy_payload if parsed.trailer_present else legacy_payload
    unknown = parsed.unknown_blocks if preserve_unknown else ()

    icon_mime = detect_image_mime(icon)
    if icon_mime is None:
        raise MetadataValidationError("ICON must be PNG or JPEG image bytes")
    if screenshot is not None and detect_image_mime(screenshot) is None:
        raise MetadataValidationError("SCRN must be PNG or JPEG image bytes")

    normalized_colophon = normalize_colophon(colophon) if colophon is not None else None
    if normalized_colophon is None:
        warnings.warn(
            "building a metadata trailer without COLO; the Cartrige Store "
            "prefers colophon-complete cartridges",
            stacklevel=2,
        )
    normalized_metadata = normalize_metadata(
        metadata,
        architecture=architecture,
        has_screenshot=screenshot is not None,
        has_signature=signature is not None,
        has_colophon=normalized_colophon is not None,
    )
    if normalized_metadata["assets"]["icon"].get("mime") == "image/png" and icon_mime:
        normalized_metadata["assets"]["icon"]["mime"] = icon_mime
    if screenshot is not None:
        normalized_metadata["assets"]["screenshot"]["mime"] = detect_image_mime(screenshot)

    blocks: list[TrailerBlock] = [
        TrailerBlock(BLOCK_META, deterministic_json_bytes(normalized_metadata)),
        TrailerBlock(BLOCK_ICON, icon),
    ]
    if screenshot is not None:
        blocks.append(TrailerBlock(BLOCK_SCREENSHOT, screenshot))
    if signature is not None:
        if isinstance(signature, dict):
            signature_data = deterministic_json_bytes(signature)
        else:
            signature_data = signature
        blocks.append(TrailerBlock(BLOCK_SIGNATURE, signature_data))
    if normalized_colophon is not None:
        blocks.append(TrailerBlock(BLOCK_COLOPHON, deterministic_json_bytes(normalized_colophon)))
    blocks.extend(unknown)
    blocks.extend(extra_blocks)
    return base_payload + build_trailer(blocks)


def parse_file(path: str | Path) -> CartridgeImage:
    return parse_cartridge(Path(path).read_bytes())


def build_from_files(
    cartridge: str | Path,
    *,
    metadata_path: str | Path,
    icon_path: str | Path,
    out_path: str | Path,
    screenshot_path: str | Path | None = None,
    signature_path: str | Path | None = None,
    colophon_path: str | Path | None = None,
    architecture: str | None = None,
) -> None:
    signature: bytes | dict[str, Any] | None = None
    if signature_path is not None:
        signature_bytes = Path(signature_path).read_bytes()
        try:
            decoded = json.loads(signature_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            signature = signature_bytes
        else:
            signature = decoded if isinstance(decoded, dict) else signature_bytes
    image = build_cartridge(
        Path(cartridge).read_bytes(),
        metadata=_read_json(metadata_path),
        icon=Path(icon_path).read_bytes(),
        screenshot=Path(screenshot_path).read_bytes() if screenshot_path else None,
        signature=signature,
        colophon=_read_json(colophon_path) if colophon_path else None,
        architecture=architecture,
    )
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Path(out_path).write_bytes(image)


def summary_dict(parsed: CartridgeImage) -> dict[str, Any]:
    return {
        "trailer_present": parsed.trailer_present,
        "legacy_size": len(parsed.legacy_payload),
        "blocks": [
            {"type": block.block_type, "length": len(block.data)}
            for block in parsed.blocks
        ],
        "metadata": parsed.metadata,
        "icon": {
            "present": parsed.icon is not None,
            "length": len(parsed.icon or b""),
            "mime": detect_image_mime(parsed.icon or b"") if parsed.icon else None,
        },
        "screenshot": {
            "present": parsed.screenshot is not None,
            "length": len(parsed.screenshot or b""),
            "mime": detect_image_mime(parsed.screenshot or b"")
            if parsed.screenshot else None,
        },
        "signature": {
            "present": parsed.signature is not None,
            "length": len(parsed.signature or b""),
            "json": parsed.signature_json,
        },
        "colophon": parsed.colophon,
        "unknown_blocks": [
            {"type": block.block_type, "length": len(block.data)}
            for block in parsed.unknown_blocks
        ],
    }
