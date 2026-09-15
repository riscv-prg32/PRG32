#!/usr/bin/env python3

import argparse
import os
from prg32.utilities.logging import step, log_ok, log_info, die
from prg32.utilities.env_variables import ROOT_DIR
from prg32.utilities.local_config import set_local_config, remove_local_config, LOCAL_CONFIG_FILE

def wifi_set(args: argparse.Namespace):
    ssid = args.ssid
    password = args.password
    mode = args.mode

    if len(password) > 0 and len(password) < 8:
        die("WPA2 requires a password of at least 8 characters.")

    step("Setting local WiFi configuration...")
    
    updates = {}
    if mode == "ap":
        updates["CONFIG_PRG32_WIFI_BOOT_MODE_AP"] = "y"
        updates["CONFIG_PRG32_WIFI_BOOT_MODE_STA"] = "n"
    elif mode in ["sta", "infrastructure"]:
        updates["CONFIG_PRG32_WIFI_BOOT_MODE_AP"] = "n"
        updates["CONFIG_PRG32_WIFI_BOOT_MODE_STA"] = "y"
    
    updates["CONFIG_PRG32_WIFI_SSID"] = f'"{ssid}"'
    updates["CONFIG_PRG32_WIFI_PASSWORD"] = f'"{password}"'
    
    set_local_config(updates)
    
    log_ok(f"Saved WiFi configuration to {LOCAL_CONFIG_FILE}")
    log_info("These settings will be automatically applied on your next build.")

def wifi_clear(args: argparse.Namespace):
    step("Clearing local WiFi configuration...")
    remove_local_config([
        "CONFIG_PRG32_WIFI_BOOT_MODE_AP",
        "CONFIG_PRG32_WIFI_BOOT_MODE_STA",
        "CONFIG_PRG32_WIFI_SSID",
        "CONFIG_PRG32_WIFI_PASSWORD"
    ])
    log_ok("Cleared WiFi settings from local configuration.")
