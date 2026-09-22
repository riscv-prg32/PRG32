from __future__ import annotations

import argparse
import io
import json
from pathlib import Path
import contextlib
import tempfile
import unittest

from prg32.utilities import partition_handler, runtime_handler, env_variables
from prg32.cartridge import build_cartridge
from prg32.store import metadata as store_metadata
from prg32.qemu import upload_qemu
from prg32.utilities.environment_check import doctor
from prg32.abi.abi_generated import (
    ABI_HASH,
    COMPATIBLE_ABI_HASHES,
    FEATURE_BITS,
    IMPORT_NAMES,
)
from prg32.abi.abi_gen import abi_hash
from prg32.prg32 import main as prg32_main

ROOT = Path(__file__).resolve().parents[1]

class PortableBuildPolicyTests(unittest.TestCase):
    def test_firmware_specific_build_is_rejected_before_toolchain_runs(self) -> None:
        with self.assertRaisesRegex(SystemExit, "firmware-specific cartridge builds"):
            build_cartridge.build_cartridge_core(
                source="unused.S",
                out="unused.prg32",
                entry_prefix="unused",
                tool_prefix="riscv32-esp-elf-",
                march="rv32imc_zicsr_zifencei",
                mabi="ilp32",
                portable=False,
            )

class PartitionParsingTests(unittest.TestCase):
    def test_parse_partition_size_units(self) -> None:
        self.assertEqual(partition_handler.parse_partition_size("512K"), 512 * 1024)
        self.assertEqual(partition_handler.parse_partition_size("2M"), 2 * 1024 * 1024)
        self.assertEqual(partition_handler.parse_partition_size("0x210000"), 0x210000)

    def test_read_partition_slot(self) -> None:
        offset, size = partition_handler.read_partition_slot(
            ROOT / "partitions_prg32.csv",
            "cart0",
        )
        self.assertEqual(offset, 0x210000)
        self.assertEqual(size, 128 * 1024)


class CartridgeSizeCompatibilityTests(unittest.TestCase):
    def test_legacy_32_kib_cartridge_remains_accepted(self) -> None:
        runtime_handler.ensure_cart_max_size(b"\0" * (32 * 1024))

    def test_64_kib_limit_is_inclusive(self) -> None:
        runtime_handler.ensure_cart_max_size(b"\0" * (64 * 1024))

        with self.assertRaises(SystemExit):
            runtime_handler.ensure_cart_max_size(b"\0" * (64 * 1024 + 1))

class SymbolParsingTests(unittest.TestCase):
    def test_parse_nm_ignores_non_symbol_lines(self) -> None:
        symbols = runtime_handler.parse_nm(
            """
00000000 T prg32_ticks_ms
not-a-symbol
00000010 D prg32_cart_exec
"""
        )
        self.assertEqual(symbols["prg32_ticks_ms"], 0)
        self.assertEqual(symbols["prg32_cart_exec"], 0x10)

    def test_imports_include_platform_tile_helpers(self) -> None:
        self.assertIn("prg32_playfield_draw_dual", IMPORT_NAMES)
        self.assertIn("prg32_platform_actor_step", IMPORT_NAMES)
        self.assertIn("prg32_platform_camera_follow", IMPORT_NAMES)

    def test_imports_include_audio_plus_helpers(self) -> None:
        self.assertIn("prg32_audio_play_sample_pan", IMPORT_NAMES)
        self.assertIn("prg32_audio_note_on_pan", IMPORT_NAMES)
        self.assertIn("prg32_audio_get_mode", IMPORT_NAMES)

    def test_imports_include_splash_helpers(self) -> None:
        self.assertIn("prg32_splash_draw", IMPORT_NAMES)
        self.assertIn("prg32_splash_show", IMPORT_NAMES)
        self.assertIn("prg32_splash_show_default", IMPORT_NAMES)

    def test_detect_entries_accepts_c_prefix(self) -> None:
        entries = build_cartridge.detect_entries(
            {
                "platformer_c_init": 0,
                "platformer_c_update": 4,
                "platformer_c_draw": 8,
            },
            "platformer_c",
        )
        self.assertEqual(
            entries,
            ("platformer_c_init", "platformer_c_update", "platformer_c_draw"),
        )

class PortableHeaderTests(unittest.TestCase):
    def test_compatible_hashes_describe_unchanged_abi_prefixes(self) -> None:
        abi = json.loads((ROOT / "prg32/abi/prg32_abi.json").read_text())
        for hash_value, minor, count in (
            (0x006427C2, 5, 133),
            (0x6BE6E8D0, 5, 138),
            (0x260F6136, 6, 139),
        ):
            prior = {**abi, "minor": minor, "functions": abi["functions"][:count]}
            self.assertEqual(abi_hash(prior), hash_value)

    def test_runtime_rejects_old_hashes_with_reassigned_audio_slots(self) -> None:
        self.assertEqual(COMPATIBLE_ABI_HASHES, [0x006427C2, 0x6BE6E8D0, 0x260F6136])
        payload = b"\0\0\0\0"
        for old_hash in (0xEC21EFE2, 0x5626CB8A):
            header = env_variables.CART_HEADER_V2.pack(
                env_variables.CART_MAGIC,
                env_variables.CART_ABI_MAJOR,
                1,
                env_variables.CART_HEADER_V2.size,
                env_variables.PRG32_CART_FLAG_ABI_TABLE,
                env_variables.FALLBACK_CART_LOAD_ADDR,
                len(payload), len(payload), 0, 0, 0, 0,
                b"old" + b"\0" * 29,
                old_hash,
                0, 0, 0, 0, 0,
                env_variables.PRG32_IMPORT_MODEL_ABI_TABLE,
            )
            with self.assertRaisesRegex(SystemExit, "portable ABI hash"):
                runtime_handler.validate_cartridge_contract(header + payload)

    def test_runtime_accepts_older_append_only_portable_cartridges(self) -> None:
        payload = b"\0\0\0\0"
        for old_hash in COMPATIBLE_ABI_HASHES:
            header = env_variables.CART_HEADER_V2.pack(
                env_variables.CART_MAGIC,
                env_variables.CART_ABI_MAJOR,
                1,
                env_variables.CART_HEADER_V2.size,
                env_variables.PRG32_CART_FLAG_ABI_TABLE,
                env_variables.FALLBACK_CART_LOAD_ADDR,
                len(payload), len(payload), 0, 0, 0, 0,
                b"old" + b"\0" * 29,
                old_hash,
                0, 0, 0, 0, 0,
                env_variables.PRG32_IMPORT_MODEL_ABI_TABLE,
            )
            runtime_handler.validate_cartridge_contract(header + payload)
            runtime_handler.validate_cartridge_contract(
                header + payload,
                runtime={"cart_abi_hash": ABI_HASH},
            )

    def test_current_cartridge_is_rejected_by_an_older_runtime(self) -> None:
        payload = b"\0\0\0\0"
        header = env_variables.CART_HEADER_V2.pack(
            env_variables.CART_MAGIC,
            env_variables.CART_ABI_MAJOR,
            1,
            env_variables.CART_HEADER_V2.size,
            env_variables.PRG32_CART_FLAG_ABI_TABLE,
            env_variables.FALLBACK_CART_LOAD_ADDR,
            len(payload), len(payload), 0, 0, 0, 0,
            b"new" + b"\0" * 29,
            ABI_HASH,
            0, 0, 0, 0, 0,
            env_variables.PRG32_IMPORT_MODEL_ABI_TABLE,
        )
        with self.assertRaisesRegex(SystemExit, "portable ABI hash"):
            runtime_handler.validate_cartridge_contract(
                header + payload,
                runtime={"cart_abi_hash": 0x6BE6E8D0},
            )

    def test_performance_abi_is_appended_after_indexed_graphics(self) -> None:
        self.assertEqual(IMPORT_NAMES[122], "prg32_sprite_draw_indexed")
        self.assertEqual(IMPORT_NAMES[123], "prg32_sprite_draw_bitplanes")
        self.assertEqual(
            IMPORT_NAMES[124:133],
            [
                "prg32_perf_now_us", "prg32_perf_begin",
                "prg32_perf_case_begin", "prg32_perf_record",
                "prg32_perf_case_end", "prg32_perf_end",
                "prg32_perf_abort", "prg32_perf_get_state",
                "prg32_perf_get_summary",
            ],
        )
        self.assertEqual(
            IMPORT_NAMES[133:138],
            [
                "prg32_palette_set", "prg32_palette_get",
                "prg32_gfx_pixel_indexed", "prg32_gfx_rect_indexed",
                "prg32_gfx_clear_indexed",
            ],
        )
        self.assertEqual(IMPORT_NAMES[138], "prg32_random_number")

    def test_bluetooth_keyboard_abi_is_appended_after_random(self) -> None:
        self.assertEqual(
            IMPORT_NAMES[139:146],
            [
                "prg32_btkbd_state", "prg32_btkbd_read_key",
                "prg32_btkbd_modifiers", "prg32_btkbd_key_down",
                "prg32_btkbd_flush", "prg32_btkbd_set_mapping",
                "prg32_btkbd_device_name",
            ],
        )
        self.assertEqual(len(IMPORT_NAMES), 146)

    def test_portable_build_uses_position_tolerant_riscv_flags(self) -> None:
        # We look in build_cartridge.py now
        TOOL_PATH = ROOT / "prg32" / "cartridge" / "build_cartridge.py"
        text = TOOL_PATH.read_text(encoding="utf-8")

        self.assertIn('"-mcmodel=medany"', text)
        self.assertIn('"-msmall-data-limit=0"', text)

    def test_v2_header_records_abi_table_import_model(self) -> None:
        header = env_variables.CART_HEADER_V2.pack(
            env_variables.CART_MAGIC,
            env_variables.CART_ABI_MAJOR,
            env_variables.CART_ABI_MINOR,
            env_variables.CART_HEADER_V2.size,
            env_variables.PRG32_CART_FLAG_ABI_TABLE,
            env_variables.FALLBACK_CART_LOAD_ADDR,
            4,
            4,
            0,
            0,
            0,
            0,
            b"test" + b"\0" * 28,
            ABI_HASH,
            FEATURE_BITS["audio"],
            FEATURE_BITS["sprites"],
            0,
            0,
            0,
            env_variables.PRG32_IMPORT_MODEL_ABI_TABLE,
        )
        with tempfile.TemporaryDirectory() as tmp:
            cart = Path(tmp) / "portable.prg32"
            cart.write_bytes(header + b"\0\0\0\0")
            buf = io.StringIO()
            args = argparse.Namespace(cartridge=str(cart))
            with contextlib.redirect_stdout(buf):
                store_metadata.inspect_metadata(args)
        text = buf.getvalue()
        self.assertIn('"import_model": "abi-table"', text)
        self.assertIn(f'"abi_hash": "0x{ABI_HASH:08x}"', text)


class QemuUploadTests(unittest.TestCase):
    def test_upload_qemu_stages_cartridge_at_partition_offset(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            flash = tmp_path / "qemu_flash.bin"
            cart = tmp_path / "game.prg32"
            partitions = tmp_path / "partitions.csv"

            payload = b"\0\0\0\0"
            header = env_variables.CART_HEADER_V2.pack(
                env_variables.CART_MAGIC,
                env_variables.CART_ABI_MAJOR,
                env_variables.CART_ABI_MINOR,
                env_variables.CART_HEADER_V2.size,
                env_variables.PRG32_CART_FLAG_ABI_TABLE,
                env_variables.FALLBACK_CART_LOAD_ADDR,
                len(payload),
                len(payload),
                0,
                0,
                0,
                0,
                b"test" + b"\0" * 28,
                ABI_HASH,
                0,
                0,
                0,
                0,
                0,
                env_variables.PRG32_IMPORT_MODEL_ABI_TABLE,
            )
            flash.write_bytes(b"\x00" * 256)
            cart.write_bytes(header + payload)
            partitions.write_text(
                "cart0, data, 0x40, 0x10, 128,\n",
                encoding="utf-8",
            )

            args = argparse.Namespace(
                flash=str(flash),
                cartridge=str(cart),
                partitions=str(partitions),
                slot="cart0",
            )
            upload_qemu.upload_qemu(args)

            data = flash.read_bytes()
            self.assertEqual(data[0x10:0x14], b"PRG2")
            erased = data[0x10 + len(header) + len(payload):0x10 + 128]
            self.assertEqual(erased, b"\xff" * len(erased))


def portable_header(mem_size: int, code_size: int = 4) -> bytes:
    """Pack a minimal portable v2 header that declares mem_size bytes of RAM."""
    return env_variables.CART_HEADER_V2.pack(
        env_variables.CART_MAGIC,
        env_variables.CART_ABI_MAJOR,
        env_variables.CART_ABI_MINOR,
        env_variables.CART_HEADER_V2.size,
        env_variables.PRG32_CART_FLAG_ABI_TABLE,
        env_variables.FALLBACK_CART_LOAD_ADDR,
        code_size, mem_size, 0, 0, 0, 0,
        b"ram" + b"\0" * 29,
        ABI_HASH,
        0, 0, 0, 0, 0,
        env_variables.PRG32_IMPORT_MODEL_ABI_TABLE,
    )


class CartRamProfileTests(unittest.TestCase):
    # A cartridge that fits the 64 KiB extended profile but not the
    # 32 KiB classroom profile.
    MEM_SIZE_40_KIB = 40 * 1024

    def test_fallback_matches_default_firmware_profile(self) -> None:
        self.assertEqual(env_variables.DEFAULT_CART_RAM_KIB, 64)
        self.assertEqual(env_variables.FALLBACK_CART_RAM_SIZE, 64 * 1024)
        for defaults in ("sdkconfig.defaults", "sdkconfig.defaults.qemu"):
            text = (ROOT / defaults).read_text(encoding="utf-8")
            self.assertIn("CONFIG_PRG32_CART_RAM_EXTENDED=y", text, defaults)

    def test_contract_uses_64_kib_without_runtime_info(self) -> None:
        data = portable_header(self.MEM_SIZE_40_KIB) + b"\0\0\0\0"
        runtime_handler.validate_cartridge_contract(data)
        with self.assertRaisesRegex(SystemExit, "executable RAM"):
            runtime_handler.validate_cartridge_contract(
                data,
                runtime={"cart_ram_size": 32 * 1024},
            )

    def test_parse_cart_ram_kib_uses_kconfig_range(self) -> None:
        self.assertEqual(runtime_handler.parse_cart_ram_kib("32"), 32)
        self.assertEqual(runtime_handler.parse_cart_ram_kib("128"), 128)
        for bad in ("8", "256", "big"):
            with self.assertRaises(argparse.ArgumentTypeError):
                runtime_handler.parse_cart_ram_kib(bad)

    def test_resolve_prefers_option_then_sdkconfig_then_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sdkconfig = Path(tmp) / "sdkconfig"
            self.assertEqual(
                runtime_handler.resolve_cart_ram_size(None, sdkconfig)[0],
                64 * 1024,
            )
            sdkconfig.write_text(
                "CONFIG_PRG32_CART_RAM_CLASSROOM=y\n"
                "CONFIG_PRG32_CART_RAM_KIB=32\n",
                encoding="utf-8",
            )
            self.assertEqual(
                runtime_handler.resolve_cart_ram_size(None, sdkconfig),
                (32 * 1024, str(sdkconfig)),
            )
            self.assertEqual(
                runtime_handler.resolve_cart_ram_size(128, sdkconfig)[0],
                128 * 1024,
            )

    def _stage(self, tmp_path: Path, cart_ram_kib: int | None) -> None:
        flash = tmp_path / "qemu_flash.bin"
        cart = tmp_path / "big.prg32"
        partitions = tmp_path / "partitions.csv"
        flash.write_bytes(b"\x00" * 512)
        cart.write_bytes(portable_header(self.MEM_SIZE_40_KIB) + b"\0\0\0\0")
        partitions.write_text("cart0, data, 0x40, 0x10, 256,\n", encoding="utf-8")
        upload_qemu.upload_qemu(argparse.Namespace(
            flash=str(flash),
            cartridge=str(cart),
            partitions=str(partitions),
            slot="cart0",
            cart_ram_kib=cart_ram_kib,
        ))

    def test_qemu_upload_accepts_extended_cartridge_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            self._stage(Path(tmp), None)

    def test_qemu_upload_honours_classroom_option_and_sdkconfig(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, "executable RAM"):
                self._stage(Path(tmp), 32)
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "sdkconfig").write_text(
                "CONFIG_PRG32_CART_RAM_KIB=32\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(SystemExit, "executable RAM"):
                self._stage(Path(tmp), None)
            # An explicit option overrides the sdkconfig profile.
            self._stage(Path(tmp), 64)


class ModuleImportTests(unittest.TestCase):
    def test_every_prg32_module_imports(self) -> None:
        # Catches stale imports left behind by tooling refactors, such as
        # helpers importing names that env_variables no longer defines.
        import importlib
        import pkgutil
        import prg32

        for module in pkgutil.walk_packages(prg32.__path__, "prg32."):
            with self.subTest(module=module.name):
                importlib.import_module(module.name)

    def test_inject_cartridge_is_shared_by_qemu_helpers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            flash = tmp_path / "qemu_flash.bin"
            cart = tmp_path / "game.prg32"
            partitions = tmp_path / "partitions.csv"
            flash.write_bytes(b"\x00" * 256)
            cart.write_bytes(portable_header(4) + b"\0\0\0\0")
            partitions.write_text("cart0, data, 0x40, 0x10, 128,\n", encoding="utf-8")
            upload_qemu.inject_cartridge(
                str(cart),
                flash=str(flash),
                partitions=str(partitions),
            )
            self.assertEqual(flash.read_bytes()[0x10:0x14], b"PRG2")


class DoctorTests(unittest.TestCase):
    def test_host_only_doctor_does_not_require_esp_idf(self) -> None:
        rc = prg32_main(["doctor", "--host-only", "--partitions", str(ROOT / "partitions_prg32.csv")])
        self.assertEqual(rc, 0)


if __name__ == "__main__":
    unittest.main()
