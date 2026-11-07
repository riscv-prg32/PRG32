#!/usr/bin/env python3
"""Download a PRG32 firmware release from GitHub and flash it on an ESP32-C6.

Stand-alone: this one file needs Python 3.9 or newer and nothing from ESP-IDF
or the PRG32 SDK. To talk to the chip's ROM bootloader it uses esptool, the
small flasher package published on PyPI. If esptool is missing, the tool offers
to install it into its own virtual environment (~/.prg32/flasher-venv), so the
system Python is left unchanged.

Typical use, with the board on its connector labelled USB:

    python3 prg32_flash_release.py                 # pick a release and profile
    python3 prg32_flash_release.py --list          # list the releases
    python3 prg32_flash_release.py --release v1.8.0 --profile breadboard1

Only the bootloader, the partition table, and the application are written.
Wi-Fi settings, joystick calibration, the eight cartridge slots, and scores
are kept, unless --erase-all is given.

Release format: docs/usage/firmware_releases.md (prg32-firmware-release-1.0).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_REPO = "riscv-prg32/PRG32"
RELEASE_FORMAT = "prg32-firmware-release-1.0"
MANIFEST_NAME = "prg32-firmware.json"
USER_AGENT = "prg32-flash-release/1.0"

# The ESP32-C6 native USB Serial/JTAG port (the connector labelled USB).
ESPRESSIF_USB_VID = 0x303A
ESPRESSIF_USB_JTAG_PID = 0x1001

# esptool 4.x and 5.x both accept the write_flash spelling used below.
ESPTOOL_REQUIREMENT = "esptool>=4.7,<6"
VENV_DIR = Path.home() / ".prg32" / "flasher-venv"
DEFAULT_CACHE_DIR = Path.home() / ".prg32" / "firmware-cache"


class FlashError(Exception):
    """A problem the user can fix; printed without a traceback."""


# --------------------------------------------------------------------------
# esptool: find it, or install it into a private virtual environment
# --------------------------------------------------------------------------

def esptool_available(python: str) -> bool:
    result = subprocess.run(
        [python, "-c", "import esptool, serial.tools.list_ports"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def venv_python() -> Path:
    if os.name == "nt":
        return VENV_DIR / "Scripts" / "python.exe"
    return VENV_DIR / "bin" / "python"


def ensure_esptool(assume_yes: bool) -> str:
    """Return a Python interpreter that can import esptool and pyserial."""
    if esptool_available(sys.executable):
        return sys.executable
    private = venv_python()
    if private.exists() and esptool_available(str(private)):
        return str(private)

    print("esptool (Espressif's flasher package) is needed to talk to the board.")
    print(f"It can be installed into a private environment: {VENV_DIR}")
    if not assume_yes and not ask_yes_no("Install it now?", default=True):
        raise FlashError(f"install it yourself with:  {sys.executable} -m pip install \"{ESPTOOL_REQUIREMENT}\"")
    if not private.exists():
        subprocess.run([sys.executable, "-m", "venv", str(VENV_DIR)], check=True)
    subprocess.run([str(private), "-m", "pip", "install", "--quiet", "--upgrade", "pip"], check=True)
    subprocess.run([str(private), "-m", "pip", "install", "--quiet", ESPTOOL_REQUIREMENT], check=True)
    if not esptool_available(str(private)):
        raise FlashError("esptool was installed but cannot be imported")
    return str(private)


# --------------------------------------------------------------------------
# Talking to GitHub
# --------------------------------------------------------------------------

def http_get(url: str, accept: str = "application/json") -> bytes:
    headers = {"User-Agent": USER_AGENT, "Accept": accept}
    token = os.environ.get("GITHUB_TOKEN")
    if token and url.startswith("https://api.github.com/"):
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        if exc.code == 403 and "api.github.com" in url:
            raise FlashError("GitHub refused the request (rate limit?); set GITHUB_TOKEN or retry later") from exc
        raise FlashError(f"{url}: HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise FlashError(f"{url}: {exc.reason}") from exc


def firmware_releases(repo: str, include_prereleases: bool) -> list[dict]:
    """Releases that carry a prg32-firmware.json asset, newest first."""
    data = json.loads(http_get(f"https://api.github.com/repos/{repo}/releases?per_page=30"))
    releases = []
    for release in data:
        if release.get("draft"):
            continue
        if release.get("prerelease") and not include_prereleases:
            continue
        assets = {asset["name"]: asset["browser_download_url"] for asset in release.get("assets", [])}
        if MANIFEST_NAME in assets:
            releases.append({
                "tag": release["tag_name"],
                "name": release.get("name") or release["tag_name"],
                "published": (release.get("published_at") or "")[:10],
                "prerelease": bool(release.get("prerelease")),
                "assets": assets,
            })
    return releases


def select_release(releases: list[dict], wanted: str | None) -> dict:
    if not releases:
        raise FlashError("no firmware release with a prg32-firmware.json asset was found")
    if wanted in (None, "", "latest"):
        return releases[0]
    for release in releases:
        if release["tag"] == wanted:
            return release
    tags = ", ".join(release["tag"] for release in releases)
    raise FlashError(f"release {wanted!r} not found; available: {tags}")


# --------------------------------------------------------------------------
# The release manifest
# --------------------------------------------------------------------------

def validate_manifest(manifest: dict) -> None:
    if manifest.get("format") != RELEASE_FORMAT:
        raise FlashError(f"unsupported release format {manifest.get('format')!r}; this tool reads {RELEASE_FORMAT}")
    if manifest.get("chip") != "esp32c6":
        raise FlashError(f"this release targets {manifest.get('chip')!r}, not esp32c6")
    profiles = manifest.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        raise FlashError("the release lists no pin profiles")
    for profile in profiles:
        parts = profile.get("parts") or []
        roles = {part.get("role") for part in parts}
        if roles != {"bootloader", "partition-table", "app"}:
            raise FlashError(f"profile {profile.get('name')!r} must have bootloader, partition-table, and app parts")
        for part in parts:
            int(part["offset"], 0)
            if len(part.get("sha256", "")) != 64:
                raise FlashError(f"part {part.get('file')!r} has no SHA-256")


def select_profile(manifest: dict, wanted: str | None) -> dict:
    profiles = manifest["profiles"]
    if wanted:
        for profile in profiles:
            if profile["name"] == wanted:
                return profile
        names = ", ".join(profile["name"] for profile in profiles)
        raise FlashError(f"profile {wanted!r} not in this release; available: {names}")
    print("\nPin profiles in this release (see docs/hardware/profiles/):")
    for index, profile in enumerate(profiles, 1):
        print(f"  {index}. {profile['name']:<12} {profile.get('description', '')}")
    return profiles[ask_choice("Profile", len(profiles)) - 1]


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch_part(part: dict, source: str, cache_dir: Path) -> Path:
    """Return a local, checksum-verified copy of one flash part.

    ``source`` is either a directory holding the release files or a base URL
    ending in '/', both relative to the manifest.
    """
    target = cache_dir / part["file"]
    if target.exists() and sha256_of(target) == part["sha256"]:
        return target
    if "://" in source:
        print(f"  downloading {part['file']} ({part['size']} bytes)")
        data = http_get(source + part["file"], accept="application/octet-stream")
        cache_dir.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    else:
        local = Path(source) / part["file"]
        if not local.exists():
            raise FlashError(f"missing {local}")
        target = local
    if sha256_of(target) != part["sha256"]:
        raise FlashError(f"{part['file']} does not match the SHA-256 in the manifest; download it again")
    return target


def load_manifest(args: argparse.Namespace) -> tuple[dict, str, str]:
    """Return (manifest, base for part files, label)."""
    if args.manifest:
        if "://" in args.manifest:
            manifest = json.loads(http_get(args.manifest))
            return manifest, args.manifest.rsplit("/", 1)[0] + "/", args.manifest
        path = Path(args.manifest).expanduser().resolve()
        return json.loads(path.read_text(encoding="utf-8")), str(path.parent), str(path)
    releases = firmware_releases(args.repo, args.prereleases)
    if args.release is None and sys.stdin.isatty() and len(releases) > 1:
        print(f"Firmware releases of {args.repo}:")
        for index, release in enumerate(releases, 1):
            print(f"  {index}. {release['tag']:<12} {release['published']}  {release['name']}")
        release = releases[ask_choice("Release", len(releases)) - 1]
    else:
        release = select_release(releases, args.release)
    manifest_url = release["assets"][MANIFEST_NAME]
    manifest = json.loads(http_get(manifest_url, accept="application/octet-stream"))
    return manifest, manifest_url.rsplit("/", 1)[0] + "/", f"{args.repo} {release['tag']}"


# --------------------------------------------------------------------------
# Serial port
# --------------------------------------------------------------------------

def find_board_port(python: str) -> str:
    """Find the one ESP32-C6 native USB port, using pyserial from esptool's environment."""
    script = (
        "import json, serial.tools.list_ports as lp\n"
        "print(json.dumps([[p.device, p.vid, p.pid] for p in lp.comports()]))\n"
    )
    output = subprocess.run([python, "-c", script], check=True, capture_output=True, text=True).stdout
    ports = [device for device, vid, pid in json.loads(output)
             if vid == ESPRESSIF_USB_VID and pid == ESPRESSIF_USB_JTAG_PID]
    # macOS lists each USB device as /dev/cu.* and /dev/tty.*; prefer cu.
    ports = sorted(set(ports), key=lambda name: (not name.startswith("/dev/cu."), name))
    if sys.platform == "darwin":
        ports = [name for name in ports if not name.startswith("/dev/tty.")] or ports
    if not ports:
        raise FlashError(
            "no ESP32-C6 found on a native USB port. Plug the cable into the board's "
            "connector labelled USB (not UART), use a data cable, or pass --port"
        )
    if len(ports) > 1:
        raise FlashError(f"several ESP32-C6 boards are connected ({', '.join(ports)}); choose one with --port")
    return ports[0]


# --------------------------------------------------------------------------
# Flashing
# --------------------------------------------------------------------------

def esptool_command(python: str, port: str, baud: int, manifest: dict, files: list[tuple[str, Path]]) -> list[str]:
    flash = manifest.get("flash", {})
    command = [
        python, "-m", "esptool",
        "--chip", manifest["chip"],
        "--port", port,
        "--baud", str(baud),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", flash.get("flash_mode", "dio"),
        "--flash_freq", flash.get("flash_freq", "80m"),
        "--flash_size", flash.get("flash_size", "4MB"),
    ]
    for offset, path in files:
        command += [offset, str(path)]
    return command


def erase_command(python: str, port: str, baud: int, chip: str) -> list[str]:
    return [python, "-m", "esptool", "--chip", chip, "--port", port, "--baud", str(baud),
            "--before", "default_reset", "--after", "no_reset", "erase_flash"]


# --------------------------------------------------------------------------
# Prompts
# --------------------------------------------------------------------------

def ask_yes_no(question: str, default: bool) -> bool:
    if not sys.stdin.isatty():
        return default
    suffix = " [Y/n] " if default else " [y/N] "
    answer = input(question + suffix).strip().lower()
    return default if not answer else answer in ("y", "yes")


def ask_choice(label: str, count: int) -> int:
    if count == 1 or not sys.stdin.isatty():
        return 1
    while True:
        answer = input(f"{label} [1-{count}, default 1]: ").strip() or "1"
        if answer.isdigit() and 1 <= int(answer) <= count:
            return int(answer)
        print(f"Type a number from 1 to {count}.")


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Download a PRG32 firmware release from GitHub and flash it on an ESP32-C6.",
        epilog="Wi-Fi settings, calibration, cartridges, and scores are kept unless --erase-all is given.",
    )
    parser.add_argument("--list", action="store_true", help="list the firmware releases and their profiles, then stop")
    parser.add_argument("--release", help="release tag to flash, or 'latest' (default: ask, or the newest)")
    parser.add_argument("--profile", help="pin profile: breadboard1, breadboard2, pcb1 (default: ask)")
    parser.add_argument("--port", help="serial port of the board (default: find the ESP32-C6 native USB port)")
    parser.add_argument("--baud", type=int, default=460800, help="flashing baud rate (default 460800)")
    parser.add_argument("--repo", default=DEFAULT_REPO, help=f"GitHub repository (default {DEFAULT_REPO})")
    parser.add_argument("--manifest", help="a prg32-firmware.json file or URL instead of a GitHub release")
    parser.add_argument("--prereleases", action="store_true", help="also offer pre-releases")
    parser.add_argument("--cache-dir", default=str(DEFAULT_CACHE_DIR), help="where downloads are kept")
    parser.add_argument("--erase-all", action="store_true",
                        help="erase the whole flash first: deletes Wi-Fi settings, ALL cartridges, and scores")
    parser.add_argument("--download-only", action="store_true", help="download and verify, do not flash")
    parser.add_argument("--dry-run", action="store_true", help="print the esptool commands instead of running them")
    parser.add_argument("-y", "--yes", action="store_true", help="answer yes to every question")
    return parser


def list_releases(args: argparse.Namespace) -> None:
    releases = firmware_releases(args.repo, args.prereleases)
    if not releases:
        print(f"{args.repo} has no firmware releases with {MANIFEST_NAME}.")
        return
    for release in releases:
        manifest = json.loads(http_get(release["assets"][MANIFEST_NAME], accept="application/octet-stream"))
        abi = manifest.get("abi", {})
        profiles = ", ".join(profile["name"] for profile in manifest.get("profiles", []))
        flag = " (pre-release)" if release["prerelease"] else ""
        print(f"{release['tag']}{flag}  {release['published']}  ABI {abi.get('major')}.{abi.get('minor')}  profiles: {profiles}")


def run(args: argparse.Namespace) -> int:
    if args.list:
        list_releases(args)
        return 0

    manifest, source, label = load_manifest(args)
    validate_manifest(manifest)
    profile = select_profile(manifest, args.profile)
    abi = manifest.get("abi", {})
    print(f"\nFirmware {manifest['version']} from {label}")
    print(f"  ABI {abi.get('major')}.{abi.get('minor')} ({abi.get('hash')}), profile {profile['name']}: {profile.get('description', '')}")

    cache_dir = Path(args.cache_dir).expanduser() / manifest["version"]
    files = [(part["offset"], fetch_part(part, source, cache_dir))
             for part in sorted(profile["parts"], key=lambda part: int(part["offset"], 0))]
    print("  all parts match their SHA-256")
    if args.download_only:
        for offset, path in files:
            print(f"  {offset:>8}  {path}")
        return 0

    if profile.get("warning"):
        print(f"\nWARNING: {profile['warning']}")
        if not args.yes and not ask_yes_no("Flash this profile anyway?", default=False):
            return 1

    python = sys.executable if args.dry_run and not esptool_available(sys.executable) else ensure_esptool(args.yes)
    port = args.port or ("<port>" if args.dry_run and not esptool_available(python) else find_board_port(python))

    commands = []
    if args.erase_all:
        print("\n--erase-all deletes Wi-Fi settings, joystick calibration, ALL eight cartridges, and scores.")
        if not args.yes and not ask_yes_no("Erase the whole flash?", default=False):
            return 1
        commands.append(erase_command(python, port, args.baud, manifest["chip"]))
    else:
        print("  kept on the board: " + ", ".join(manifest.get("preserves", [])))
    commands.append(esptool_command(python, port, args.baud, manifest, files))

    if args.dry_run:
        for command in commands:
            print(" ".join(command))
        return 0

    if not args.yes and not ask_yes_no(f"Flash {manifest['version']} ({profile['name']}) to {port}?", default=True):
        return 1
    for command in commands:
        subprocess.run(command, check=True)
    print(f"\nDone. The board restarts into PRG32 {manifest['version']}.")
    print("If the screen stays dark, press RESET once (the chip may still be in its bootloader).")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return run(args)
    except FlashError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"error: esptool failed with exit code {exc.returncode}", file=sys.stderr)
        return exc.returncode or 1
    except KeyboardInterrupt:
        print("\ncancelled", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
