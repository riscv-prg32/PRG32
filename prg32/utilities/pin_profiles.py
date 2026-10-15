#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
from prg32.utilities.env_variables import ROOT_DIR

PROFILES_DIR = ROOT_DIR / "profiles"


def list_pin_profiles(profiles_dir: Path | None = None) -> dict[str, Path]:
    """
    Scans profiles directory for hardware pin profiles.
    Matches profiles named `sdkconfig.pins.<name>` or files inside `pins/<name>.defaults`.
    Returns a mapping of profile name -> Path.
    """
    base_dir = profiles_dir or PROFILES_DIR
    profiles: dict[str, Path] = {}
    if not base_dir.exists():
        return profiles

    # Look for sdkconfig.pins.<name>
    for f in base_dir.glob("sdkconfig.pins.*"):
        if f.is_file():
            name = f.name[len("sdkconfig.pins."):].strip().lower()
            if name:
                profiles[name] = f

    # Also look in a pins/ subdirectory if present
    pins_subdir = base_dir / "pins"
    if pins_subdir.is_dir():
        for f in pins_subdir.glob("*.defaults"):
            if f.is_file():
                name = f.stem.strip().lower()
                if name:
                    profiles[name] = f

    return profiles


def resolve_pin_profile(name: str, profiles_dir: Path | None = None) -> Path:
    """
    Resolves a pin profile name or path to a valid Path.
    If name is not found, raises ValueError listing available profiles.
    """
    if not name:
        raise ValueError("No pin profile specified.")

    # Check direct path first if specified as a path
    direct_path = Path(name)
    if direct_path.is_file():
        return direct_path

    available = list_pin_profiles(profiles_dir)
    lookup_name = name.strip().lower()

    if lookup_name in available:
        return available[lookup_name]

    avail_names = ", ".join(sorted(available.keys())) if available else "(none found)"
    raise ValueError(
        f"Unknown pin profile '{name}'. Available pin profiles: {avail_names}"
    )
