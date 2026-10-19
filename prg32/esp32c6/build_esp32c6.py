#!/usr/bin/env python3

import subprocess
import argparse
from prg32.utilities.logging import *
from prg32.utilities.pin_profiles import resolve_pin_profile, list_pin_profiles
from prg32.utilities.local_config import erase_sdkconfigs, apply_local_config_to_sdkconfig
from prg32.utilities.env_variables import (
    ESP32C6_BUILD_DIR,
    ESP32C6_SDKCONFIG,
    ESP32C6_SDKCONFIG_DEFAULTS,
    ESP32C6_ELF,
    ROOT_DIR,
    DEFAULT_PIN_PROFILE,
)

def list_pins_esp32c6(args: argparse.Namespace | None = None) -> None:
    profiles = list_pin_profiles()
    print("Available PRG32 hardware pin profiles:")
    for name, path in sorted(profiles.items()):
        print(f"  - {name} ({path.name})")

def get_sdkconfig_pin_profile() -> tuple[str, bool]:
    """
    Inspects existing sdkconfig files (build-esp32c6/sdkconfig or root sdkconfig)
    and profiles/sdkconfig.defaults.local for a default pin profile selected via
    menuconfig or local override.
    Settings in sdkconfig.defaults.local always prevail over base sdkconfig.
    Returns (profile_name, is_custom).
    """
    from pathlib import Path
    candidate_files = []
    for cand in [Path(ESP32C6_SDKCONFIG), ROOT_DIR / "sdkconfig"]:
        if cand.is_file():
            candidate_files.append(cand)
            break
    local_cfg = ROOT_DIR / "profiles" / "sdkconfig.defaults.local"
    if local_cfg.is_file():
        candidate_files.append(local_cfg)

    profile_name = ""
    is_custom = False
    for cand in candidate_files:
        content = cand.read_text(encoding="utf-8", errors="ignore")
        for line in content.splitlines():
            line = line.strip()
            if line == "CONFIG_PRG32_PIN_PROFILE_CUSTOM=y":
                is_custom = True
            elif line.startswith("CONFIG_PRG32_DEFAULT_PIN_PROFILE="):
                val = line.split("=", 1)[1].strip().strip('"')
                if val and val.lower() != "none":
                    profile_name = val
                    is_custom = False
            elif line == "CONFIG_PRG32_PIN_PROFILE_PROTOTYPE1=y" and not profile_name:
                profile_name = "prototype1"
                is_custom = False
            elif line == "CONFIG_PRG32_PIN_PROFILE_PROTOTYPE2=y" and not profile_name:
                profile_name = "prototype2"
                is_custom = False

    if is_custom:
        return ("custom", True)
    if profile_name:
        return (profile_name, False)
    return ("", False)

def get_esp32c6_sdkconfig_defaults(args: argparse.Namespace) -> str:
    defaults = ESP32C6_SDKCONFIG_DEFAULTS
    pin_profile = getattr(args, 'pin_profile', None) or DEFAULT_PIN_PROFILE
    if not pin_profile:
        cfg_profile, is_custom = get_sdkconfig_pin_profile()
        if is_custom:
            # Custom profile: pins are configured directly in sdkconfig by menuconfig.
            pin_profile = None
        elif cfg_profile:
            pin_profile = cfg_profile
        else:
            die(
                "No PRG32 hardware pin profile configured!\n"
                "Pins are no longer defaulted in Kconfig.\n"
                "Specify a pin profile when building, e.g.:\n"
                "  python3 -m prg32 esp32c6 build --pin-profile prototype1\n"
                "See available pin setup possibilities with:\n"
                "  python3 -m prg32 esp32c6 pins\n"
                "Or configure a pin profile (or Custom Profile) in 'idf.py menuconfig'."
            )

    if pin_profile:
        try:
            profile_path = resolve_pin_profile(pin_profile)
            try:
                rel_path = profile_path.relative_to(ROOT_DIR)
                defaults += f";{rel_path}"
            except ValueError:
                defaults += f";{profile_path}"
        except ValueError as e:
            die(str(e))

    if getattr(args, 'debug', False):
        defaults += ";profiles/sdkconfig.defaults.debug"

    local_defaults = ROOT_DIR / "profiles" / "sdkconfig.defaults.local"
    if local_defaults.is_file():
        defaults += ";profiles/sdkconfig.defaults.local"

    return defaults

def set_target_esp32c6(args: argparse.Namespace):
    defaults = get_esp32c6_sdkconfig_defaults(args)
    step("Configuring ESP32C6 target (esp32c6)...")
    import os, shutil
    if os.path.exists(ESP32C6_BUILD_DIR):
        shutil.rmtree(ESP32C6_BUILD_DIR, ignore_errors=True)
    subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={defaults}", "set-target", "esp32c6"])

def build_esp32c6(args: argparse.Namespace):
    if not (getattr(args, 'no_erase_sdkconfig', False) or getattr(args, 'keep_sdkconfig', False)):
        erase_sdkconfigs("esp32c6")
    defaults = get_esp32c6_sdkconfig_defaults(args)
    if args.skip_target:
        log_info("Skipping setting target to ESP32C6...")
    else:
        set_target_esp32c6(args)

    import os
    enable_tracking = getattr(args, 'enable_heap_tracking', False)
    
    # If sdkconfig was erased and skip_target was used, ensure it is reconfigured before appending
    if not os.path.exists(ESP32C6_SDKCONFIG):
        subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={defaults}", "reconfigure"])

    # Force configurations by directly appending to the generated sdkconfig.
    # Kconfig uses the last defined value in the file.
    if os.path.exists(ESP32C6_SDKCONFIG):
        with open(ESP32C6_SDKCONFIG, "a") as f:
            f.write(f"\nCONFIG_HEAP_TASK_TRACKING={'y' if enable_tracking else 'n'}\n")

    # Ensure profiles/sdkconfig.defaults.local is applied at the end so it always prevails
    apply_local_config_to_sdkconfig("esp32c6")

    step("Building ESP32C6...")
    defaults = get_esp32c6_sdkconfig_defaults(args)
    subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={defaults}", "build"])
    log_ok(f"Created {ESP32C6_BUILD_DIR} directory.")

    # Verify
    if not (Path := __import__('pathlib').Path)(ESP32C6_ELF).exists():
        die(f"Missing {ESP32C6_ELF} after build.")
    log_ok("Firmware build ready")

def flash_esp32c6(args: argparse.Namespace):
    step("Flashing the ESP32C6 firmware and flash_image via USB")
    defaults = get_esp32c6_sdkconfig_defaults(args)
    subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={defaults}", "flash", "monitor"])

def erase_flash_esp32c6(args: argparse.Namespace):
    step("Erasing ESP32C6 flash...")
    subprocess.check_call(["idf.py", "erase-flash"])
    log_ok("Done erasing ESP32C6 flash")

def reset_esp32c6(args: argparse.Namespace):
    if not (getattr(args, 'no_erase_sdkconfig', False) or getattr(args, 'keep_sdkconfig', False)):
        erase_sdkconfigs("esp32c6")
    erase_flash_esp32c6(args)
    flash_esp32c6(args)

