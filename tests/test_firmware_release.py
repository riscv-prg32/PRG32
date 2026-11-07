"""Tests for the firmware release packager and the stand-alone flasher.

No network and no board: the packager runs on a fake ESP-IDF build directory,
and the flasher reads the manifest it produced from a local directory.
"""

import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


release = load_tool("prg32_firmware_release")
flasher = load_tool("prg32_flash_release")


def fake_build(directory: Path, version: str, app_bytes: bytes) -> None:
    (directory / "bootloader").mkdir(parents=True)
    (directory / "partition_table").mkdir()
    (directory / "bootloader" / "bootloader.bin").write_bytes(b"boot")
    (directory / "partition_table" / "partition-table.bin").write_bytes(b"table")
    (directory / "PRG32.bin").write_bytes(app_bytes)
    (directory / "flasher_args.json").write_text(json.dumps({
        "flash_settings": {"flash_mode": "dio", "flash_size": "4MB", "flash_freq": "80m"},
        "bootloader": {"offset": "0x0", "file": "bootloader/bootloader.bin"},
        "app": {"offset": "0x10000", "file": "PRG32.bin"},
        "partition-table": {"offset": "0x8000", "file": "partition_table/partition-table.bin"},
        "extra_esptool_args": {"chip": "esp32c6"},
    }))
    (directory / "project_description.json").write_text(json.dumps({"project_version": version}))


class FirmwareReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.out = self.root / "assets"
        with redirect_stdout(io.StringIO()):
            for profile in ("breadboard1", "pcb1"):
                build = self.root / f"build-{profile}"
                fake_build(build, "v9.9.9", f"app-{profile}".encode())
                release.main(["package", "--build-dir", str(build), "--profile", profile,
                              "--version", "v9.9.9", "--out-dir", str(self.out)])
            release.main(["manifest", "--out-dir", str(self.out), "--commit", "abc123",
                          "--esp-idf", "v5.4.1",
                          "--abi-header", str(ROOT / "components/prg32/include/prg32_abi_hash.h")])
        self.manifest = json.loads((self.out / release.MANIFEST_NAME).read_text())

    def tearDown(self):
        self.tmp.cleanup()

    def test_manifest_lists_every_profile_with_three_parts_in_offset_order(self):
        self.assertEqual(self.manifest["format"], "prg32-firmware-release-1.0")
        self.assertEqual([p["name"] for p in self.manifest["profiles"]], ["breadboard1", "pcb1"])
        for profile in self.manifest["profiles"]:
            self.assertEqual([part["offset"] for part in profile["parts"]], ["0x0", "0x8000", "0x10000"])
        self.assertIn("warning", self.manifest["profiles"][1])
        self.assertEqual(self.manifest["abi"]["major"], 1)
        self.assertTrue(self.manifest["abi"]["hash"].startswith("0x"))

    def test_nvs_and_cartridge_slots_are_never_written(self):
        self.assertIn("nvs", self.manifest["preserves"])
        self.assertIn("cart7", self.manifest["preserves"])
        for profile in self.manifest["profiles"]:
            for part in profile["parts"]:
                # NVS starts at 0x9000: no part may begin inside 0x9000..0xffff.
                self.assertFalse(0x9000 <= int(part["offset"], 0) < 0x10000)

    def test_checksums_cover_every_asset(self):
        names = {line.split()[1] for line in (self.out / "SHA256SUMS").read_text().splitlines()}
        self.assertIn(release.MANIFEST_NAME, names)
        self.assertEqual(len(names), 1 + 2 * 3)
        self.assertFalse(list(self.out.glob("profile-*.json")))

    def test_package_refuses_a_dirty_or_wrong_version(self):
        build = self.root / "build-dirty"
        fake_build(build, "v9.9.9-dirty", b"app")
        with self.assertRaises(SystemExit):
            release.main(["package", "--build-dir", str(build), "--profile", "breadboard2",
                          "--version", "v9.9.9", "--out-dir", str(self.root / "other")])

    def test_flasher_accepts_the_manifest_and_verifies_parts(self):
        flasher.validate_manifest(self.manifest)
        profile = flasher.select_profile(self.manifest, "breadboard1")
        cache = self.root / "cache"
        paths = [flasher.fetch_part(part, str(self.out), cache) for part in profile["parts"]]
        self.assertTrue(all(path.exists() for path in paths))

        broken = dict(profile["parts"][2], sha256="0" * 64)
        with self.assertRaises(flasher.FlashError):
            flasher.fetch_part(broken, str(self.out), cache)

    def test_flasher_rejects_unknown_formats_and_profiles(self):
        with self.assertRaises(flasher.FlashError):
            flasher.validate_manifest(dict(self.manifest, format="prg32-firmware-release-2.0"))
        with self.assertRaises(flasher.FlashError):
            flasher.select_profile(self.manifest, "breadboard9")

    def test_esptool_command_writes_each_part_at_its_offset(self):
        files = [("0x0", Path("b.bin")), ("0x8000", Path("t.bin")), ("0x10000", Path("a.bin"))]
        command = flasher.esptool_command("python", "/dev/ttyACM0", 460800, self.manifest, files)
        self.assertEqual(command[:3], ["python", "-m", "esptool"])
        self.assertIn("write_flash", command)
        self.assertNotIn("erase_flash", command)
        self.assertEqual(command[-6:], ["0x0", "b.bin", "0x8000", "t.bin", "0x10000", "a.bin"])

    def test_dry_run_from_a_local_manifest_needs_no_network(self):
        output = io.StringIO()
        with redirect_stdout(output):
            code = flasher.main(["--manifest", str(self.out / release.MANIFEST_NAME), "--profile",
                                 "breadboard1", "--port", "/dev/ttyACM0", "--dry-run",
                                 "--cache-dir", str(self.root / "cache")])
        self.assertEqual(code, 0)
        self.assertIn("write_flash", output.getvalue())

    def test_select_release_picks_newest_or_named_tag(self):
        releases = [{"tag": "v2.0.0"}, {"tag": "v1.8.0"}]
        self.assertEqual(flasher.select_release(releases, None)["tag"], "v2.0.0")
        self.assertEqual(flasher.select_release(releases, "v1.8.0")["tag"], "v1.8.0")
        with self.assertRaises(flasher.FlashError):
            flasher.select_release(releases, "v0.0.1")


if __name__ == "__main__":
    unittest.main()
