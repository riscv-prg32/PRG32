# Network Setup and Wi-Fi Modes

PRG32 relies on Wi-Fi connectivity for several core features, including cartridge uploading over HTTP, connecting to the Cartridge Store, uploading telemetry/scores, and cartridge-level multiplayer sharing.

## Wi-Fi Modes

PRG32 supports three Wi-Fi runtime modes:

- `PRG32_WIFI_MODE_STA` (Station): connect to an existing infrastructure access point (e.g., your home or classroom router).
- `PRG32_WIFI_MODE_AP` (Access Point): create the PRG32 access point so external devices can connect to it.
- `PRG32_WIFI_MODE_APSTA` (Access Point + Station): keep the upload AP running while also connecting to an existing infrastructure router.

## Setup Menu Configuration

The **Wi-Fi setup screen** lets the user choose between access-point mode or infrastructure mode. You can access the setup menu by holding the **A** and **B** buttons simultaneously during boot.

- **Infrastructure mode** scans for nearby SSIDs, lists them on screen, and uses UP/DOWN plus SELECT or B to select. A cancels back.
- The setup UI also shows the active Wi-Fi mode and IP address; AP mode shows the AP SSID, while infrastructure mode shows the selected/connected SSID.

The chosen network settings are stored persistently in non-volatile storage (NVS) under the `prg32wifi` namespace.

## Technical Details

When running in Access Point mode (`PRG32_WIFI_MODE_AP` or `PRG32_WIFI_MODE_APSTA`), the default classroom values configured in `main/prg32_config.h` are:

- **SSID:** `PRG32`
- **Password:** `prg32game`
- **IP Address:** `192.168.4.1`

In Station mode (`PRG32_WIFI_MODE_STA`), the IP address will be assigned by the DHCP server of the router you connect to.

### QEMU Networking

QEMU builds keep physical Wi-Fi disabled by default (`CONFIG_PRG32_DISPLAY_QEMU_RGB`), but games can still compile against the same API and exercise setup screens. However, mDNS, CartridgeStore downloads, and multiplayer capabilities generally require specific forwarding or bridging on the virtual QEMU network to function, or they rely on local configuration overrides.

## Advanced: Pre-configuring Wi-Fi Credentials

For advanced users or developers who frequently flash the firmware, it can be tedious to manually configure the network via the setup menu on every full wipe. 

You can use the PRG32 Python tooling to set your Wi-Fi credentials *before* building. These credentials will be securely merged into your local build environment without touching tracked source files.

To set your Wi-Fi configuration, run the following command in the PRG32 root:

```bash
python3 -m prg32 wifi set --ssid "YOUR_WIFI_SSID" --password "YOUR_WIFI_PASSWORD" --mode sta
```

- `--mode`: Can be `sta` (connect to an existing network) or `ap` (create your own access point).
- The tooling saves this to `profiles/sdkconfig.defaults.local`, which is ignored by Git, ensuring you don't accidentally commit your network password.
- You only need to run this command once. It will be remembered for all future builds.

To clear the credentials and return to the default behavior:

```bash
python3 -m prg32 wifi clear
```
## Development Guide

> [!IMPORTANT]
> This information is only intended for developers of the PRG32 framework.

### Multiplayer Implementation Details

- `prg32_input_read_player(2)` returns `0`; multiplayer games should use the
  cartridge multiplayer API for remote player snapshots.
- Cartridge multiplayer groups peers by cartridge signature and uses ESP32-C6
  Wi-Fi station mode plus WebSocket to a Node.js `ws` server.
