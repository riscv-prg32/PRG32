import argparse
from prg32.utilities.logging import step, log_ok, log_info, die
from prg32.utilities.local_config import set_local_config, remove_local_config, LOCAL_CONFIG_FILE

def multiplayer_set_url(args: argparse.Namespace):
    url = args.url
    if not url.startswith(("ws://", "wss://")):
        die("The multiplayer server address must start with ws:// or wss://")

    step("Setting local multiplayer server address...")
    set_local_config({"CONFIG_PRG32_MULTIPLAYER_SERVER_URL": f'"{url}"'})
    log_ok(f"Saved multiplayer server address to {LOCAL_CONFIG_FILE}")
    log_info("It applies to the next build that generates a new sdkconfig. "
             "The address is compiled into the firmware, so flash it again afterwards.")

def multiplayer_clear_url(args: argparse.Namespace):
    step("Clearing local multiplayer server address...")
    remove_local_config(["CONFIG_PRG32_MULTIPLAYER_SERVER_URL"])
    log_ok("Cleared the multiplayer server address from local configuration.")
