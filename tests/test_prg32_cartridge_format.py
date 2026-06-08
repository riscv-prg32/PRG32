from __future__ import annotations

import binascii
import importlib.util
from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "prg32_cartridge_format.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("prg32_cartridge_format", TOOL_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {TOOL_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


fmt = load_tool()

PNG_1X1 = (
    b"\x89PNG\r\n\x1a\n"
    b"\x00\x00\x00\rIHDR"
    b"\x00\x00\x00\x01\x00\x00\x00\x01"
    b"\x08\x02\x00\x00\x00\x90wS\xde"
    b"\x00\x00\x00\x0cIDATx\x9cc```\x00\x00\x00\x04\x00\x01"
    b"\xf6\x178U\x00\x00\x00\x00IEND\xaeB`\x82"
)

JPEG_TINY = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xd9"


def fake_legacy_cart(name: str = "test") -> bytes:
    code = b"\x13\x00\x00\x00" * 3
    crc = binascii.crc32(code) & 0xffffffff
    header = struct.pack(
        "<4sHHHHIIIIIII32s",
        b"PRG2",
        1,
        0,
        fmt.CART_HEADER.size,
        0,
        0x40380000,
        len(code),
        len(code),
        0,
        4,
        8,
        crc,
        name.encode("ascii")[:31] + b"\0" * (32 - len(name.encode("ascii")[:31])),
    )
    assert len(header) == fmt.CART_HEADER.size
    return header + code


def metadata() -> dict:
    return {
        "abi": "prg32-metadata-1.0",
        "id": "edu.prg32.test",
        "title": "Test Cart",
        "version": "1.0.0",
        "summary": "A small test cartridge",
        "authors": [{"name": "PRG32"}],
        "tags": ["test"],
        "runtime": {"platform": "PRG32", "isa": "RV32I"},
    }


def colophon() -> dict:
    return {
        "abi": "prg32-colophon-1.0",
        "title": "Test Cart",
        "version": "1.0.0",
        "developer": {"name": "PRG32"},
        "authors": [],
        "controls": [],
    }


class CartridgeMetadataFormatTests(unittest.TestCase):
    def test_build_and_parse_full_metadata_trailer(self) -> None:
        image = fmt.build_cartridge(
            fake_legacy_cart(),
            metadata=metadata(),
            icon=PNG_1X1,
            screenshot=JPEG_TINY,
            signature={"algorithm": "none", "value": "classroom"},
            colophon=colophon(),
            architecture="esp32c6",
        )
        parsed = fmt.parse_cartridge(image)

        self.assertTrue(parsed.trailer_present)
        self.assertEqual(parsed.legacy_payload, fake_legacy_cart())
        self.assertEqual(parsed.metadata["id"], "edu.prg32.test")
        self.assertEqual(parsed.metadata["runtime"]["architecture"], "esp32c6")
        self.assertEqual(parsed.icon, PNG_1X1)
        self.assertEqual(parsed.screenshot, JPEG_TINY)
        self.assertEqual(parsed.signature_json["algorithm"], "none")
        self.assertEqual(parsed.colophon["title"], "Test Cart")

    def test_build_with_metadata_icon_and_colophon_only(self) -> None:
        image = fmt.build_cartridge(
            fake_legacy_cart(),
            metadata=metadata(),
            icon=PNG_1X1,
            colophon=colophon(),
            architecture="qemu",
        )
        parsed = fmt.parse_cartridge(image)

        self.assertEqual(
            [block.block_type for block in parsed.blocks],
            ["META", "ICON", "COLO"],
        )
        self.assertIsNone(parsed.screenshot)
        self.assertEqual(parsed.metadata["runtime"]["architecture"], "qemu")

    def test_parse_legacy_without_trailer(self) -> None:
        legacy = fake_legacy_cart()
        parsed = fmt.parse_cartridge(legacy)

        self.assertFalse(parsed.trailer_present)
        self.assertEqual(parsed.legacy_payload, legacy)
        self.assertEqual(parsed.blocks, ())

    def test_parse_malformed_trailer_fails_safely(self) -> None:
        malformed = fake_legacy_cart() + b"PRG32META"

        with self.assertRaises(fmt.CartridgeFormatError):
            fmt.parse_cartridge(malformed)

    def test_unknown_tlv_blocks_are_preserved(self) -> None:
        unknown = fmt.TrailerBlock("X123", b"opaque")
        with_unknown = fake_legacy_cart() + fmt.build_trailer([unknown])

        rebuilt = fmt.build_cartridge(
            with_unknown,
            metadata=metadata(),
            icon=PNG_1X1,
            colophon=colophon(),
        )
        parsed = fmt.parse_cartridge(rebuilt)

        self.assertEqual(parsed.unknown_blocks, (unknown,))

    def test_colophon_required_fields_are_validated(self) -> None:
        bad = {
            "abi": "prg32-colophon-1.0",
            "title": "Missing developer",
            "version": "1.0.0",
            "authors": [],
            "controls": [],
        }

        with self.assertRaises(fmt.ColophonValidationError):
            fmt.validate_colophon(bad)


if __name__ == "__main__":
    unittest.main()
