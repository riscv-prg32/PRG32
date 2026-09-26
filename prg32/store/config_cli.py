import argparse
from prg32.utilities.logging import step, log_ok, log_info
from prg32.utilities.local_config import set_local_config, remove_local_config, LOCAL_CONFIG_FILE

def store_set_url(args: argparse.Namespace):
    url = args.url
    
    step("Setting local CartridgeStore URL...")
    
    updates = {
        "CONFIG_PRG32_STORE_URL": f'"{url}"'
    }
    
    set_local_config(updates)
    
    log_ok(f"Saved CartridgeStore URL to {LOCAL_CONFIG_FILE}")
    log_info("This setting will be automatically applied on your next build.")

def store_clear_url(args: argparse.Namespace):
    step("Clearing local CartridgeStore URL...")
    remove_local_config(["CONFIG_PRG32_STORE_URL"])
    log_ok("Cleared CartridgeStore URL from local configuration.")
