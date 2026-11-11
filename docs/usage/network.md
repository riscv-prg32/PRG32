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
- **The scan stops the radio**, so the console is off the network, and its HTTP API does not answer, from the scan until you leave these screens. Leaving without connecting, with B from the network list, keyboard, or confirmation screen, or because a cartridge [took over](../software/usb_protocol.md#device-behavior), starts the saved Wi-Fi mode again; a station reconnects and the HTTP API usually answers within 5 to 15 s, though it took up to 35 s in testing. Nothing you chose on these screens is saved until you confirm it.
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
python3 -m prg32 esp32c6 wifi-scan
```

```bash
python3 -m prg32 esp32c6 wifi-set --ssid LabNet
```

```bash
python3 -m prg32 esp32c6 wifi-forget --ssid LabNet
```

Each command finds the connected board itself; add `--port` when several
boards are connected (see [tooling](tooling.md#upload--run-cartridges)).
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

QEMU builds have no Wi-Fi (`CONFIG_PRG32_DISPLAY_QEMU_RGB`), but games can still compile against the same API and exercise setup screens. mDNS, CartridgeStore downloads, and the HTTP API do not run on QEMU.

Multiplayer does: the firmware starts QEMU's emulated OpenCores Ethernet the first time a cartridge joins a room, and gets an address from QEMU's user-mode network. The console reaches any address the computer can reach, without configuration on the computer. A QEMU build follows the same order as the board, below, so with nothing saved or compiled in it uses the public server. From inside QEMU, `10.0.2.2` means the computer running QEMU: to play against a server running there, choose **SERVER URL** in the setup menu and type `10.0.2.2` (the port step offers 8081).

## Multiplayer Server Address

No [multiplayer server](../software/multiplayer_protocol.md) address is built into the
firmware. When a cartridge joins a room, the console uses the first of these that
it has:

| Order | Source | Set with |
|---|---|---|
| 1 | A server found on the local network | Nothing: mDNS, then LAN discovery, [below](#finding-the-server-on-the-local-network). On by default; **LOCAL SERVER: OFF** skips it |
| 2 | The address typed on the console | **SETUP > MULTIPLAYER SETUP > SERVER URL** |
| 3 | An address compiled into this firmware, if you chose one | `python3 -m prg32 multiplayer set-url` |
| 4 | The public server, `ws://193.205.230.7:8099` | `CONFIG_PRG32_MULTIPLAYER_PUBLIC_SERVER_URL`; empty turns it off |

A server on the local network comes first because it answers fastest, which
matters in a classroom. The console searches for one each time it connects to Wi-Fi,
at boot and after a reconnection, so a local server is usually known before a game
starts; when a game joins without one, it searches again. With nothing found and no
address typed or compiled in, it turns to the public server after 10 seconds. A
local server that stops answering for 10 seconds gives way to rows 2 to 4. A server
that works is never replaced during a game.

To skip the local server, for example to play with someone on another network,
turn **LOCAL SERVER** off: the console then never searches, and goes straight to
rows 2 to 4.

The public server is reached directly and **unencrypted** for now, so the identity a
console sends when it joins crosses the internet in clear text. It moves to
`wss://multi.prg32.uniparthenope.it` once that address forwards WebSocket
connections ([hosted server](../software/multiplayer_protocol.md#hosted-server-as-the-public-fallback-2026-11-11)).

### Setting the Server on the Console

Open **SETUP > MULTIPLAYER SETUP**:

| Item | What it does |
|---|---|
| SERVER URL | Opens the keyboard. Type an IP address or host name such as `192.168.1.10`; a second keyboard then asks for the **SERVER PORT**, already filled in with `8081` (press `return` to keep it). Typing `host:port` or a full `ws://` or `wss://` URL skips the port step (`:` and `/` are on the keyboard's `ascii` page). Editing a saved `ws://` address offers its host and port again |
| SEARCH LOCAL SERVER | Searches now with mDNS, then LAN discovery, for up to 10 seconds (hold B to stop), and turns **LOCAL SERVER** on. A server found is shown as `FOUND: LOCAL` and used from then on |
| LOCAL SERVER: ON / OFF | Whether a server on the local network comes first. On is the default |
| RESET TO DEFAULT | Forgets the typed address, so the fallback is the compiled address or the public server again. **LOCAL SERVER** is left as it is |

Below the items, the screen shows `SERVER: LOCAL` once a local server is found (its
address is not shown), and under `ELSE:` the address the console falls back to: the
typed one, the compiled one, or the public server. A local server found is
remembered until the console restarts, and a later search that finds another one
replaces it. The typed address and the on/off choice are kept in NVS (namespace
`prg32`, keys `mp_url` and `mp_local`), survive firmware updates, and apply to the
next cartridge that joins a room. The screen works on QEMU too, except the search,
since QEMU has no mDNS and its network carries no broadcasts.

### Compiling an Address In (Optional)

A classroom that always uses one server can compile its address in:

```bash
python3 -m prg32 multiplayer set-url ws://192.168.1.10:8081
```

This saves `CONFIG_PRG32_MULTIPLAYER_SERVER_URL` in `profiles/sdkconfig.defaults.local`, which is ignored by Git, and applies to both firmware targets. It takes effect when a new `sdkconfig` is generated. `python3 -m prg32 esp32c6 build` always generates one. A QEMU build keeps its `sdkconfig`, so delete `build-qemu/sdkconfig` first. Then flash the firmware again, or rebuild the QEMU flash image. An address saved on the console still takes precedence.

Use `python3 -m prg32 multiplayer clear-url` to compile in no address again. Use `ws://` only on a local network you control, and `wss://` on a public one.

The QEMU defaults, `profiles/sdkconfig.defaults.qemu`, compile in no address either, so QEMU and the board behave the same. Inside QEMU's user-mode network `10.0.2.2` always means the computer running QEMU; save it with **SERVER URL**, or compile in `ws://10.0.2.2:8081`, to reach a server started there.

### Finding the Server on the Local Network

With **LOCAL SERVER** on, a board finds a server on its local network in two ways: when it connects to Wi-Fi, and while a cartridge has asked for a room and the board has no address or has had no connection to it for 3 seconds. The first answer wins.

**mDNS (DNS-SD).** Every 6 seconds the board asks for the service `_prg32mp._tcp`. The answer's address and SRV port give the URL; the optional TXT keys `scheme` (`ws` or `wss`, default `ws`) and `path` (default `/`) complete it. A computer can advertise a server it runs on port 8081 with:

```bash
dns-sd -R "PRG32 Multiplayer" _prg32mp._tcp local 8081
```

On Linux, `avahi-publish-service "PRG32 Multiplayer" _prg32mp._tcp 8081` does the same. Each keeps advertising while it runs. Add `scheme=wss path=/mp` after the port for a TLS server behind a path. `CONFIG_PRG32_MULTIPLAYER_MDNS_DISCOVERY=n` turns this off.

**LAN discovery (UDP).** Every 2 seconds the board broadcasts a discovery query on UDP port 8082. A PRG32 Multiplayer Server answers with its WebSocket URL, and the board connects there and joins as usual. The server also announces itself every 2 seconds. This needs the server's discovery on (its default, except in production containers), UDP port 8082 open on the server's computer, and a network that passes broadcasts between Wi-Fi clients; many guest and enterprise networks do not. `CONFIG_PRG32_MULTIPLAYER_DISCOVERY_PORT=0` turns it off.

Neither answer is authenticated: any host on the network can claim to be the server. On a network you do not control, save or compile the address instead.

QEMU has no mDNS, and its user-mode network does not carry broadcasts to the local network, so a QEMU console uses its saved or compiled address.

A PRG32 Multiplayer Server and a running PRG32-Qt can share one computer, but both
use port 8081: PRG32-Qt holds `127.0.0.1:8081` for its
[device protocol](../software/usb_protocol.md), and the server holds the same port on
every other address. Give clients on that computer its network address, such as
`ws://192.168.1.10:8081`. Through `127.0.0.1` they reach PRG32-Qt, which never
answers the WebSocket upgrade.

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
- See [Multiplayer protocol](../software/multiplayer_protocol.md) for the
  transport, identity, and session rules.
