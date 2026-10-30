# Network Setup and Wi-Fi Modes

PRG32 relies on Wi-Fi connectivity for several core features, including cartridge uploading over HTTP, connecting to the Cartridge Store, uploading telemetry/scores, and cartridge-level multiplayer sharing.

## Wi-Fi Modes

PRG32 supports three Wi-Fi runtime modes:

- `PRG32_WIFI_MODE_STA` (Station): connect to an existing infrastructure access point (e.g., your home or classroom router).
- `PRG32_WIFI_MODE_AP` (Access Point): create the PRG32 access point so external devices can connect to it.
- `PRG32_WIFI_MODE_APSTA` (Access Point + Station): keep the upload AP running while also connecting to an existing infrastructure router.

## Setup Menu Configuration

The **Wi-Fi setup screen** lets the user choose access-point mode, infrastructure mode, or **FORGET NETWORK**. You can access the setup menu by holding the **A** and **B** buttons simultaneously during boot.

- **Infrastructure mode** scans for nearby SSIDs, lists them on screen, and uses UP/DOWN plus SELECT or A to select. B cancels back. The password of a stored network is filled in already.
- **The scan stops the radio**, so the console is off the network, and its HTTP API does not answer, from the scan until you leave these screens. Leaving without connecting, with B from the network list, keyboard, or confirmation screen, or because a cartridge [took over](../software/usb_protocol.md#device-behavior), starts the saved Wi-Fi mode again; a station reconnects and the HTTP API answers within a few seconds. Nothing you chose on these screens is saved until you confirm it.
- **FORGET NETWORK** lists the stored networks, newest first; choose one with UP/DOWN and SELECT/A, then confirm with SELECT/A (B cancels).
- The setup UI also shows the active Wi-Fi mode and IP address; AP mode shows the AP SSID, while infrastructure mode shows the selected/connected SSID.

The chosen network settings are stored persistently in non-volatile storage (NVS) under the `prg32wifi` namespace.

## Stored Networks

The console remembers the last four infrastructure networks it connected to,
with their passwords, newest first (NVS key `known` in the `prg32wifi`
namespace). A network is added only after it has really connected and given
the console an address; joining it again moves it to the front, and a fifth
network replaces the oldest one.

At boot the console scans for its saved network. When that network is out of
range, it joins the most recently connected stored network that is in range,
so a console moved between home and the lab connects without the setup menu.

Forgetting a network erases it from the list and, when it is the network in
use, switches to the next stored network, or to access-point mode when none
is left. The ESP-IDF Wi-Fi driver keeps its own copy of the last credentials
and a cache of recent access points, so forgetting also resets the driver's
settings; the next connection may then need one extra attempt.

Erased NVS entries are only marked as erased: their bytes stay in flash until
NVS reuses the page, so a raw dump of the flash (for example with
`esptool read_flash`) can still show a forgotten password for a while.
Settings are not encrypted on the prototype; use NVS encryption where that
matters.

Stored networks can also be managed from a computer over USB with the
[device protocol](../software/usb_protocol.md#wi-fi-messages):

```bash
python3 -m prg32 esp32c6 wifi-scan --port /dev/cu.usbmodem101
```

```bash
python3 -m prg32 esp32c6 wifi-set --port /dev/cu.usbmodem101 --ssid LabNet
```

```bash
python3 -m prg32 esp32c6 wifi-forget --port /dev/cu.usbmodem101 --ssid LabNet
```

`wifi-set` asks for the password at a hidden prompt; add `--password-stdin`
to read it from standard input in scripts. The password is never accepted as
a command-line argument, so it does not end up in shell history. Never put
network names or passwords in source files, `sdkconfig`, or committed
configuration. SSIDs are 1 to 31 characters and passwords empty or 8 to 63
characters.

## Technical Details

When running in Access Point mode (`PRG32_WIFI_MODE_AP` or `PRG32_WIFI_MODE_APSTA`), the default classroom values configured in `idf.py menuconfig` are:

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

- `--mode`: Can be `sta` / `infrastructure` (connect to an existing network) or `ap` (create your own access point).
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
