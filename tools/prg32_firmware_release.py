#!/usr/bin/env python3
"""Package PRG32 ESP32-C6 firmware builds as GitHub release assets.

This script needs only the Python 3 standard library. The release workflow
(.github/workflows/release.yml) runs it in two steps:

1. ``package``: after one ESP-IDF build of one pin profile, copy the three
   flash parts (bootloader, partition table, application) with release names
   and write a per-profile JSON fragment.
2. ``manifest``: join the fragments of every profile into
   ``prg32-firmware.json`` (format ``prg32-firmware-release-1.0``) and write
   ``SHA256SUMS``.

The format is described in docs/usage/firmware_releases.md. Hosts that flash a
release (tools/prg32_flash_release.py, the Construction Kit, the Cartridge
Store) read only the manifest, never the build directory.

Why separate parts instead of one merged image: a merged image fills the gap
between the partition table (0x8000) and the application (0x10000) with 0xFF,
which erases the NVS partition at 0x9000 (Wi-Fi networks, joystick calibration,
default cartridge). Writing the three parts at their own offsets leaves NVS,
the eight cartridge slots, and the scores partition untouched.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

RELEASE_FORMAT = "prg32-firmware-release-1.0"
MANIFEST_NAME = "prg32-firmware.json"
CHECKSUMS_NAME = "SHA256SUMS"

# Short, user-facing descriptions shown by the flasher and the web pages.
# The wiring itself is documented in docs/hardware/profiles/.
PROFILE_DESCRIPTIONS = {
    "breadboard1": "First breadboard: 5-way digital joystick, buttons A and B",
    "breadboard2": "Second breadboard: analog thumbstick, buttons A, B, C, D, L, R",
    "pcb1": "Soldered PRG32 PCB 0.1: 5-way digital joystick, buttons A and B",
}

# pcb1 wires the joystick DOWN switch to GPIO13, which is USB D+ on the
# ESP32-C6. On such a board the native USB device protocol is not available,
# and flashing pcb1 onto breadboard hardware makes the USB port stop answering
# until BOOT+RESET are pressed by hand.
PROFILE_WARNINGS = {
    "pcb1": (
        "pcb1 uses GPIO13 (USB D+) for the joystick: the native USB port stops "
        "answering after this firmware boots. Flash it only on a pcb1 board; "
        "a later reflash needs BOOT+RESET pressed by hand."
    ),
}

# The partitions a release never writes. Listed in the manifest so hosts can
# tell users what is kept.
PRESERVED_PARTITIONS = [
    "nvs", "phy_init", "cart0", "cart1", "cart2", "cart3",
    "cart4", "cart5", "cart6", "cart7", "scores",
]

PART_ROLES = {
    "bootloader": "bootloader",
    "partition-table": "partition-table",
    "app": "app",
}


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def read_abi(header: Path) -> dict:
    """Read the ABI major, minor, and hash from prg32_abi_hash.h."""
    text = header.read_text(encoding="utf-8")
    values = {}
    for name in ("MAJOR", "MINOR", "HASH"):
        match = re.search(rf"#define PRG32_ABI_{name} (0x[0-9a-fA-F]+|\d+)u", text)
        if not match:
            raise SystemExit(f"PRG32_ABI_{name} not found in {header}")
        values[name] = match.group(1)
    return {
        "major": int(values["MAJOR"], 0),
        "minor": int(values["MINOR"], 0),
        "hash": "0x%08x" % int(values["HASH"], 0),
    }


def package(args: argparse.Namespace) -> None:
    build_dir = Path(args.build_dir)
    out_dir = Path(args.out_dir)
    flasher_args = json.loads((build_dir / "flasher_args.json").read_text(encoding="utf-8"))
    description = json.loads((build_dir / "project_description.json").read_text(encoding="utf-8"))

    built_version = description.get("project_version", "")
    if args.version and built_version != args.version:
        raise SystemExit(
            f"the firmware reports version {built_version!r}, expected {args.version!r}; "
            "a '-dirty' suffix means the build changed a tracked file (often dependencies.lock)"
        )
    version = args.version or built_version
    chip = flasher_args.get("extra_esptool_args", {}).get("chip", "esp32c6")

    out_dir.mkdir(parents=True, exist_ok=True)
    parts = []
    for role in PART_ROLES:
        entry = flasher_args.get(role)
        if not entry:
            raise SystemExit(f"flasher_args.json has no {role!r} entry")
        source = build_dir / entry["file"]
        name = f"prg32-firmware-{version}-{chip}-{args.profile}-{role}.bin"
        target = out_dir / name
        shutil.copyfile(source, target)
        parts.append({
            "role": role,
            "offset": entry["offset"],
            "file": name,
            "size": target.stat().st_size,
            "sha256": sha256_of(target),
        })
    parts.sort(key=lambda part: int(part["offset"], 0))

    fragment = {
        "version": version,
        "chip": chip,
        "flash": flasher_args.get("flash_settings", {}),
        "profile": {
            "name": args.profile,
            "description": PROFILE_DESCRIPTIONS.get(args.profile, args.profile),
            "parts": parts,
        },
    }
    warning = PROFILE_WARNINGS.get(args.profile)
    if warning:
        fragment["profile"]["warning"] = warning
    fragment_path = out_dir / f"profile-{args.profile}.json"
    fragment_path.write_text(json.dumps(fragment, indent=2) + "\n", encoding="utf-8")
    print(f"packaged {args.profile} ({version}) into {out_dir}")


def manifest(args: argparse.Namespace) -> None:
    out_dir = Path(args.out_dir)
    fragments = sorted(out_dir.glob("profile-*.json"))
    if not fragments:
        raise SystemExit(f"no profile-*.json fragments in {out_dir}")

    loaded = [json.loads(path.read_text(encoding="utf-8")) for path in fragments]
    versions = {fragment["version"] for fragment in loaded}
    chips = {fragment["chip"] for fragment in loaded}
    flashes = {json.dumps(fragment["flash"], sort_keys=True) for fragment in loaded}
    if len(versions) != 1 or len(chips) != 1 or len(flashes) != 1:
        raise SystemExit("profiles disagree on version, chip, or flash settings")

    release = {
        "format": RELEASE_FORMAT,
        "name": "PRG32 resident firmware",
        "version": versions.pop(),
        "commit": args.commit,
        "esp_idf": args.esp_idf,
        "chip": chips.pop(),
        "abi": read_abi(Path(args.abi_header)),
        "flash": loaded[0]["flash"],
        "preserves": PRESERVED_PARTITIONS,
        "profiles": sorted((fragment["profile"] for fragment in loaded), key=lambda p: p["name"]),
    }
    (out_dir / MANIFEST_NAME).write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
    for path in fragments:
        path.unlink()

    lines = []
    for path in sorted(out_dir.iterdir()):
        if path.name != CHECKSUMS_NAME and path.is_file():
            lines.append(f"{sha256_of(path)}  {path.name}")
    (out_dir / CHECKSUMS_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out_dir / MANIFEST_NAME} with {len(release['profiles'])} profiles")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("package", help="copy one profile's flash parts and write its fragment")
    p.add_argument("--build-dir", required=True, help="ESP-IDF build directory of one profile")
    p.add_argument("--profile", required=True, help="pin profile the build used, e.g. breadboard1")
    p.add_argument("--version", default="", help="expected firmware version (the release tag)")
    p.add_argument("--out-dir", required=True, help="directory collecting the release assets")
    p.set_defaults(func=package)

    m = sub.add_parser("manifest", help="join the profile fragments into prg32-firmware.json")
    m.add_argument("--out-dir", required=True, help="directory collecting the release assets")
    m.add_argument("--commit", default="", help="git commit the firmware was built from")
    m.add_argument("--esp-idf", default="", help="ESP-IDF version used for the build")
    m.add_argument("--abi-header", default="components/prg32/include/prg32_abi_hash.h")
    m.set_defaults(func=manifest)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
