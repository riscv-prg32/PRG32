# Multiplayer Protocol in the Firmware

PRG32 consoles share game state through the
[PRG32 Multiplayer Server](https://github.com/riscv-prg32/PRG32-Multiplayer-Server)
using the `prg32-multiplayer-1.0` protocol. That repository's `docs/protocol.md`
is the normative reference for the wire format; it is not repeated here. This
page says how the firmware implements it, what a cartridge can rely on, and how
it was tested.

For the cartridge calls, see [Multiplayer](framework_manual.md#multiplayer). For
a complete example, see [Poing Wall](../../cartridges/poingwall/README.md).

## Where things live

| File | Holds |
|---|---|
| `components/prg32/prg32_multiplayer_wire.c` | The JSON messages: building, parsing, and validating them. No networking, so `tests/test_multiplayer_wire.py` can run it on a computer |
| `components/prg32/prg32_multiplayer.c` | The session: WebSocket lifecycle, reconnection, peer table, device identity, the server address setting, mDNS and LAN discovery, and QEMU's Ethernet |
| `components/prg32/prg32_setup_multiplayer.c` | **SETUP > MULTIPLAYER SETUP**: search the local network (shown as LOCAL), type an address and its port, or clear the saved address |
| `tools/multiplayer_wall_test.py` | The test against real consoles and a real server |

## The session

```text
                      cartridge calls join
                              |
                              v
DISCONNECTED ---socket up---> CONNECTED_UNJOINED ---sends join---> JOIN_PENDING
     ^                                                                 |
     |                                                              welcome
     +------------ socket lost, or leave ------------- JOINED <--------+
```

A cartridge that calls `prg32_multiplayer_join()` is asking for membership. The
firmware keeps that request, and moves through these states by itself for as long
as the request stands.

| Rule from the protocol | What the firmware does |
|---|---|
| Admission only through `welcome` | `prg32_multiplayer_local_player_id()` returns 0 until then, and again after the connection is lost |
| No state before `welcome` | The cartridge may call `prg32_multiplayer_set_local_state()` at any time. The latest snapshot waits, and the tick sends it once admitted |
| A new socket is a new session | Peers and the player ID are forgotten on any disconnect. `join` is sent again, asking for the previous ID, and the answer is accepted whatever it is |
| Reconnection with backoff and jitter | The delay starts at 1 s, doubles to 30 s, and adds up to half of itself at random. It resets on `welcome` |
| Policy errors | `rate_limited` jumps to the longest delay. `device_blocked` ends the session until a cartridge joins again |
| Room full | The socket stays open, and `join` is repeated every 5 s |
| Peers keyed by player ID | Up to `PRG32_MP_MAX_PEERS` (8). Further peers are not tracked until a slot frees up |
| Stale snapshots | A snapshot whose `frame` is older than the one held, with 32-bit wraparound, is dropped |
| Peer expiry | 3,000 ms without a snapshot, or at once on `leave` |
| Publishing rate | 50 ms on average, from `prg32_multiplayer_tick()`. A cartridge at a lower frame rate publishes less often |

Two things go beyond the protocol's minimum:

- **`leave` also closes the socket.** Without a room, the socket would only wait
  for the server's idle timeout, and its memory is better returned to the next
  cartridge. Stopping a cartridge and opening setup both leave.
- **A cartridge must keep ticking.** The tick is what publishes, and the server
  drops a connection silent for 15 s. WebSocket pings do not count as activity
  there; the firmware uses them only to notice a dead path sooner.

## Device identity

The firmware sends a stable local identity in `join`. The server turns it into the
public pseudonym that other players and the logs see.

| Runtime | `platform` | Local identity |
|---|---|---|
| ESP32-C6 | `esp32c6` | Factory base MAC as 12 lowercase hexadecimal characters, read once and not stored again |
| QEMU | `qemu` | Random UUID, created on first use and kept in NVS (namespace `prg32id`, key `install`) |

The firmware's multiplayer code logs only what the server issued: player ID,
public device ID, and connection ID. It does not log the local identity. The Wi-Fi
driver would print the MAC at start-up, so `prg32_wifi.c` limits the driver's log
tag to warnings.

Copying a QEMU flash image after it has run copies its identity, and the protocol
forbids two instances sharing one. `prg32 qemu run --instance N` avoids this by
erasing NVS in each copy it makes. If you clone images yourself, erase that key.

## Networks

| Runtime | Path to the server |
|---|---|
| ESP32-C6 | Wi-Fi station mode. Set up the network as in [Network setup](../usage/network.md) |
| QEMU | The emulated OpenCores Ethernet, through `-nic user,model=open_eth`, which the launcher already passes. It starts at the first join, and [receives by polling](#why-qemu-polls-its-ethernet) |

No local server address is built in. The console uses the address saved in **SETUP >
MULTIPLAYER SETUP**, else one compiled in with `prg32 multiplayer set-url`, else
the first server that discovery finds, else, after 10 s of finding nothing, the
public server `CONFIG_PRG32_MULTIPLAYER_PUBLIC_SERVER_URL`
(`ws://193.205.230.7:8099` for now); see
[Multiplayer server address](../usage/network.md#multiplayer-server-address).
`prg32_multiplayer_server_url_configured()` returns the first two in that order,
and `prg32_multiplayer_server_url_current()` the one in use. These firmware calls,
declared in `prg32_multiplayer.h`, are not part of the cartridge ABI.
`wss://` verifies the server against the firmware's certificate bundle and uses a
larger task stack.

### Finding the Server

When a cartridge joins with no address known, `ensure_client()` returns 1 instead of
starting a client, and the join still succeeds: the request stands, as for any
other disconnection, and discovery starts the client when it finds a server. With
discovery compiled out, such a join fails.

A saved address is the user's choice and is used alone. `ensure_client()` records
in `g_saved_url_in_use` whether the address it starts with is the saved one, and
`discovery_needed_locked()` is false while it is, so the console never searches
then. `prg32_multiplayer_join()` therefore starts the client before discovery. A
compiled address, the public server, or no address at all leave discovery on.

The task `prg32_mp_disc` exists only while it is needed: a cartridge wants a room,
the link is down, and no saved address is in use. `start_discovery()` creates it on
`join` and on every lost connection (the WebSocket CLOSED and DISCONNECTED events),
and does nothing when it is already running. A known address first gets 3 s to
answer; with none, the task asks at once. When the console connects, or the
cartridge leaves, the task closes its socket and deletes itself, freeing its 3 KB
stack. It decides to end under `g_mp_lock`, the lock that a new start request
also takes, so a connection lost at that moment cannot be left without a task. It
asks in two ways, and the first answer wins.

**mDNS.** On ESP32-C6, every 6 s, `prg32_multiplayer_discover_mdns()` queries
the DNS-SD service `_prg32mp._tcp` for up to 1.5 s and takes the first IPv4
answer. `prg32_mp_format_server_url()` in `prg32_multiplayer_wire.c` builds
`scheme://IP:PORT/path` from the SRV port and the optional TXT keys `scheme` (`ws`
or `wss`) and `path`, and refuses anything else. The same query backs the setup
screen's scan. `CONFIG_PRG32_MULTIPLAYER_MDNS_DISCOVERY` (on; not on QEMU) builds it.

**Public fallback.** While no address is known, the task times its search. After
`PRG32_MP_PUBLIC_FALLBACK_MS` (10 s) with no answer it calls `switch_server()` with
`prg32_multiplayer_server_url_public()`. The public server then counts as the
address in use, so if it stops answering for 3 s the search on the local network
goes on and can still move the console to a classroom server. With mDNS and LAN
discovery both compiled out, `ensure_client()` uses the public server at once.

**LAN discovery,** as the server's `docs/protocol.md` ("LAN discovery") describes.
The task broadcasts `{"type":"discover","protocol":"prg32-multiplayer-1.0"}`
to `255.255.255.255:8082` every 2 s and listens on that port. The first valid
`server` message, an answer or a beacon, moves the client to its URL:
`switch_server()` destroys the client and starts a new one under the client lock,
and the CONNECTED event sends `join`. `prg32_mp_parse_discovery()` in
`prg32_multiplayer_wire.c` accepts only `type` `server`, this protocol, and a URL
that `prg32_mp_valid_server_url()` accepts: printable `ws://` or `wss://`, at most
127 characters, with a host.

The port is `CONFIG_PRG32_MULTIPLAYER_DISCOVERY_PORT` (8082; 0 builds without it).
The URL found is kept until the firmware restarts or the setting changes in
**SETUP > MULTIPLAYER SETUP**.

**Local servers.** A server found by mDNS or LAN discovery sets `g_server_is_local`
(the public fallback does not), and the setup screen shows it as `LOCAL` instead of
its address. The screen's SEARCH LOCAL NETWORK calls `prg32_multiplayer_discover_mdns()`
and `prg32_multiplayer_discover_lan()` (one UDP query, then 2 s listening) up to
five times, checking B between tries; SELECT on the result calls
`prg32_multiplayer_use_local_server()`, which clears any saved address so that the
console is back on automatic discovery, and makes the server the one in use.

**Search when Wi-Fi connects.** `prg32_wifi.c` calls `prg32_multiplayer_network_up()`
on `IP_EVENT_STA_GOT_IP`, at boot and after every reconnection. With no address
saved or compiled in, and no room requested, it creates `prg32_mp_netup` (3 KB
stack); it runs on the system event task, so it does nothing else. The task tries
mDNS and then 2 s of LAN discovery up to five times, and ends itself when a server
answers, the tries run out, a game asks for a room (the session's `prg32_mp_disc`
searches then), or setup saves an address. A server found replaces one found
before, since the console may be on another network; when none answers, the
previous one is kept.

### Why QEMU Polls Its Ethernet

With the MAC's receive interrupt enabled, an emulated console under busy traffic
stopped for good after one to five minutes. On a stopped console:

- the CPU's interrupt threshold was stuck at 4, the critical-section level, while
  the critical-section nesting count was 0;
- so every interrupt was masked, and `xTickCount` no longer moved;
- depending on the moment, the running task spun in `vListInsert` or in the wait
  after a yield, and the MAC's receive ring stayed full;
- from outside, it looked like a network fault, because QEMU kept accepting data
  for a guest that no longer read it.

It happened with both TCG threading modes. Task stacks had room to spare, and the
only interrupts above the threshold were the firmware's watchdog, cache, and debug
ones. No code in PRG32 or the driver leaves the threshold there, so this points to
how Espressif's QEMU delivers that interrupt. The emulator's source was not
examined, so that part is an inference.

The driver's receive task only needs a notification, so a small task in QEMU builds
masks the interrupt and notifies the driver every 10 ms when a frame has arrived.
After this change the problem did not appear in any of the runs below. Real
hardware does not use this code.

## The ABI

ABI 1.7 appends `prg32_multiplayer_local_player_id()`. Portable cartridges built
for earlier ABI versions still load, and a cartridge that uses the new call needs
firmware with ABI 1.7.

`prg32_multiplayer_available()` returns true only when real multiplayer is
compiled in. The `CONFIG_PRG32_MULTIPLAYER_OFFLINE_STUB` test profile lets `join`
succeed locally and reports false, as the protocol requires.

## Limits

- `wss://` is implemented but has not been tested against a TLS server, which is
  why the public fallback is plain `ws://` for now.
- mDNS and LAN discovery are not authenticated: any host on the network can answer.
  Neither works on QEMU: the build has no mDNS, and its user-mode network carries
  no broadcast.
- The hosted server does not accept consoles yet; see
  [Hosted server as the public fallback](#hosted-server-as-the-public-fallback-2026-11-11).
- The PRG32 Multiplayer Server advertises `_prg32mp._tcp` itself since its mDNS
  commit (on by default, off with `NODE_ENV=production`). An older server is found by
  mDNS only while its computer advertises it, for example with `dns-sd -R` as in
  [Finding the server on the local network](../usage/network.md#finding-the-server-on-the-local-network).
- Messages from the server arriving in several WebSocket fragments are dropped.
  The reference server sends each one as a single frame.
- The firmware does not show the public device ID on screen, or offer a QEMU
  identity reset, as the server repository's `docs/firmware-enhancement.md`
  proposes. The ID is in the serial log. To reset a QEMU identity, delete
  `build-qemu/instances/N`.
- The board's screenshot API reads the screen in bands across several frames, so
  a fast-moving ball can look cut in pieces, or look like two pieces, in a
  screenshot. Each frame draws one ball.

## Testing

`python3 -m prg32 test` includes `tests/test_multiplayer_wire.py`, which runs the
firmware's own message code on a computer against valid, malformed, and
out-of-range messages. `cartridges/poingwall/tests/run.sh` runs the example
cartridge against a scripted room.

The integration test needs a running server, QEMU, and optionally a board. Testing
on a board overwrites the slot you name, so back it up first:

```bash
python3 -m prg32 multiplayer set-url ws://192.168.1.10:8081
```

```bash
python3 -m prg32 qemu build
```

```bash
python3 tools/multiplayer_wall_test.py --server ws://192.168.1.10:8081 --qemu 3 \
  --board-port /dev/cu.usbmodem101 --board-url http://192.168.1.20 --board-slot cart7 \
  --duration 300 --out build-qemu/wall-test
```

It checks two independent sources of evidence. A silent observer in the room sees
every snapshot, and each console's framebuffer shows the ball where those
snapshots say it should be.

## Validation Record

Recorded on 2026-10-31 on macOS with ESP-IDF 5.4, Espressif QEMU 9.0.0, three QEMU
instances, and one ESP32-C6 `prototype1` board on home Wi-Fi. The room used the
Poing Wall cartridge.

The server ran from the PRG32-Multiplayer-Server working tree with the telemetry
fix below, on a separate port, with its event file seeded to its 100,000-event
limit. The firmware under test differed from the final build only in that port and
in the Wi-Fi log level, which was checked separately.

**Five-minute wall, `tools/multiplayer_wall_test.py`: all 16 checks passed.**

| Measure | Result |
|---|---|
| Publish rate | QEMU 20.0 per second; board 12.2 per second (its frame rate) |
| Longest silence from any console | 385 ms, against a 3,000 ms peer timeout |
| Leave messages, server errors | 0, 0 |
| Consoles claiming the ball | 1, the lowest player ID |
| Ball | Reached all 4 screens, 26 reversals, all at the ends of the 1,280-pixel wall, no jump over 60 px |
| Screens | Each framebuffer showed the ball on the section the traffic predicted |

A separate run of 400 s, with its monitor polled every 1.5 s, also had 0 leave
messages and 0 errors, and a longest silence of 144 ms.

**Failure scenarios, on the same running wall.**

| Scenario | Result |
|---|---|
| Kill the leading QEMU abruptly | The next-lowest ID led after 0.13 s, 7 px from where the ball was. The wall shrank to 3 screens |
| Restart that instance | Same public device ID, a new player ID. Its ID was the lowest, so it took the lead after settling |
| Stop the server for 4 s | All four back with one leader 27 s after it returned, with backoff |
| Block a device through the admin API | It stayed out for the 40 s watched, and was still out after unblocking, since only a new join brings it back |
| Switch the board to another cartridge | It left the room; free heap went from 109,872 B to 119,260 B, largest block unchanged at 83,968 B |

Not exercised against a running server: `room_full`, `rate_limited`, and `wss://`.
The first two are covered only by the wire test's classification of the error.
Messages with malformed content are covered only by the wire test.

**Server fix found by this testing.** Every relayed snapshot is a telemetry event.
At `TELEMETRY_MAX_EVENTS`, the server rewrote its whole event file for every message,
which stalled the relay for many seconds at a time: consoles saw 10 to 12 s of
silence and failed upgrades, and dropped out together. The fix, in the server's
`src/telemetry.js`, trims in batches. Recording an event at the limit then took
about 0.05 ms, with a compaction of about 31 ms every 10,000 events.

## Real Cartridges (2026-11-01)

Firmware `82b8925` on ESP32-C6 `breadboard1` and Espressif QEMU, with PRG32-Qt and
PRG32-iOS from their `feat/firmware-abi-1.8-alignment` branches, which implement this
protocol. Each console walked the game's own menus with key commands (touch in the iOS
Simulator). A silent observer recorded the room on a local server.

| Cartridge | Room | Consoles | Result |
|---|---|---|---|
| OutBun 1.1.0 | `outbun-napoli97:v1` | 2 QEMU, board, Qt | All four raced; 0 errors |
| OutBun 1.1.0 | `outbun-napoli97:v1` | QEMU, board, Qt, iOS Simulator (Release) | All four raced; 0 errors |
| FairWind 4.2.0 | `fairwind-napoli97:v8-windward` | 2 QEMU, board, Qt | All four in the room and publishing the same flags; 0 errors |
| Poing Wall | `poing-wall-v2` | 2 QEMU, board, Qt | All reported 4 screens, one authority; 0 errors |
| Galleria 2007 0.1.0 | `galleria2007-v1` | 2 QEMU, Qt | All in the room; 0 errors |
| Blackjack 1.0.0 | `blackjack-v1` | 2 QEMU, Qt | All in the room; 0 errors |
| Drumfight 1.1.0 | `drumfight-napoli97-v1` | 2 QEMU, Qt | All in the room; 0 errors |

Publish rates: 20 per second on QEMU and PRG32-Qt, 19.7 on the iOS Simulator (1.7 in a
Debug build, whose interpreter is much slower), and 8.5 to 12.4 on the board, which
follows each game's frame rate. Longest gap from any console in these runs: 0.26 s.

`tools/multiplayer_wall_test.py` with 3 QEMU instances and the board for 120 s passed
all its checks. On PRG32-Qt against a real server: with `MAX_ROOM_CLIENTS=2` it stayed out
on `room_full` and joined 2.3 s after a place freed up; after the server stopped for
4 s it rejoined; and leaving for another cartridge sent `leave`.

Not exercised: `wss://`, `rate_limited`, `device_blocked` on PRG32-Qt or PRG32-iOS, and a
physical iPhone. Reconnection and policy errors on PRG32-iOS are implemented but were not
run against a server. A cartridge on the board or QEMU joining a room lowers free heap by
about 212 B per closed socket for about two minutes: on the board six cycles took it from
107,276 B to 105,988 B, and it was back at 107,284 B within 120 s. That timing matches
TCP's TIME_WAIT; it is not a leak.

**Still open: the emulator freeze.** One QEMU instance stopped during these tests
after about 50 minutes and many uploads, with the signature described in
[Why QEMU polls its Ethernet](#why-qemu-polls-its-ethernet): threshold register 4,
critical nesting 0, spinning in `vPortYield`. The polled Ethernet therefore reduces this
freeze but does not remove it. Another instance kept running. It has not been seen on the
board. Restart the instance when it stops answering.

The tests wrote `cart7` and `scores` was unchanged; `cart7` was restored from
`esptool read_flash` and all eight slots compared with the backup.

## LAN Discovery (2026-11-06)

Firmware `1fa8474` with the LAN discovery change, built with
`python3 -m prg32 esp32c6 build -p breadboard1` and flashed with `idf.py --ccache`
after a backup of the whole flash. The board's compiled server address was
`ws://192.168.1.155:8081`, where nothing answered. The PRG32 Multiplayer Server ran
on another address of the same Wi-Fi network (`192.168.1.77:8081`), with discovery
on UDP 8082.

| Check | Result (observed) |
|---|---|
| Board opens a room with the compiled address dead | Joined the LAN server a few seconds later; the cartridge's lobby showed the room |
| A game played through the discovered server | Azzurra's Pro Tennis Challenge 2.1.0, `azzurra-tennis:v2`: the board against a QEMU console, 1 vs 1 to the result screen on both; 0 input conflicts |
| The QEMU firmware with the change | Still reaches its compiled address through QEMU's user network; discovery is not used there. Three two-console Azzurra matches all stalled after 18 to about 100 s with the scheduler stuck (`prg32_eth_poll` waiting in `vPortYield` for its cross-core interrupt) and `prg32_mp_disc` asleep in its 500 ms wait: the QEMU stall seen before this change, in 10 of 14 runs and with Poing Wall |
| `tests/test_multiplayer_wire.py` | Passes, with `prg32_mp_parse_discovery()` against valid, foreign, malformed, non-printable and too-long answers |

Not exercised: a network that drops broadcasts, two servers answering at once, and a
server lost after discovery (the firmware keeps the discovered URL and queries again
only after 3 s without a connection).

## Server Address Without a Built-in Default (2026-11-10)

The firmware no longer carries a default server address: `CONFIG_PRG32_MULTIPLAYER_SERVER_URL`
defaults to empty, the user sets the server in **SETUP > MULTIPLAYER SETUP**, and an ESP32-C6
looks for `_prg32mp._tcp` with mDNS before the UDP LAN discovery above.

| Check | Result |
|---|---|
| `python3 -m prg32 esp32c6 build -p pcb1` | Built |
| `python3 -m prg32 qemu build` | Built |
| `python3 -m prg32 test` with the parity test skipped | 203 unit tests passed; QEMU smoke test passed (HELLO, `run` upload of `pong` into `cart0`, frames advance) |
| `tests/test_multiplayer_wire.py` | Passes, with `prg32_mp_valid_server_url()` and `prg32_mp_format_server_url()` against valid, foreign-scheme, hostless, portless, relative-path, spaced and too-long input |
| `tests/test_setup_takeover.py` | Passes with the new screen's two input loops |

`test_prg32_python.ConstructionKitParityTests` failed against the sibling Construction Kit
checkout with and without this change.

Not exercised: the new screen and an mDNS answer on a running board (the attached board is
`breadboard1`, so `pcb1` firmware was built but not flashed), a saved address on QEMU, and a
join with no address while a server announces itself. No cartridge slot was written on a
board; the QEMU smoke test wrote `cart0` of its own flash image only.

**Public fallback, same day.** `CONFIG_PRG32_MULTIPLAYER_PUBLIC_SERVER_URL`
(`wss://multi.prg32.uniparthenope.it`) is used only after 10 s with no address and no local
answer. `python3 -m prg32 esp32c6 build -p pcb1` and `python3 -m prg32 qemu build` built
with it in their `sdkconfig`; `python3 -m prg32 test` (205 unit tests, parity skipped) and
the QEMU smoke test passed; the other CI checks passed. Not exercised: the fallback on a
console, and a WebSocket session with the public server, which did not yet accept upgrades
(see [Limits](#limits)).

## Hosted Server as the Public Fallback (2026-11-11)

This change was rebased onto the server-address work above. It keeps that design (no
built-in address; saved, then compiled, then mDNS and LAN discovery, then the public
server) and adds: a saved address used alone, the on-demand discovery task,
SEARCH LOCAL NETWORK showing a found server as `LOCAL`, the search when Wi-Fi
connects, and the SERVER PORT step in MANUAL ENTRY (`prg32_mp_normalize_server_url()`,
`prg32_mp_valid_port()`, `prg32_mp_split_server_url()` in `prg32_multiplayer_wire.c`).
The QEMU defaults no longer compile in `ws://10.0.2.2:8081`, so QEMU follows the
same order as the board.

`CONFIG_PRG32_MULTIPLAYER_PUBLIC_SERVER_URL` is `ws://193.205.230.7:8099`, the hosted
PRG32 Multiplayer Server with no proxy in front, because WebSocket upgrade requests
sent from a computer (no `join`) showed that the named address cannot carry
consoles yet:

| Address | Result |
|---|---|
| `http://193.205.230.7:8099/`, `/healthz`, `/api/stats` | 200: the server's status page and health; 0 connections, 0 rooms |
| `ws://193.205.230.7:8099/` (and `/ws`, `/websocket`, `/multiplayer`, `/socket`, `/api/ws`) | `403 Forbidden`, empty body: the server's `ALLOWED_ORIGINS` check refused an upgrade with no `Origin` header, which is how every console connects |
| `https://multi.prg32.uniparthenope.it/` | 200: the same status page, served by Apache (DNS 192.167.9.46) |
| `wss://multi.prg32.uniparthenope.it/` | `200 OK` with the status page instead of `101 Switching Protocols`: Apache does not pass the upgrade on |

The PRG32-Multiplayer-Server repository admits upgrades without `Origin` (browsers
are still held to `ALLOWED_ORIGINS`), ships an Apache virtual host
(`deploy/apache/prg32-multiplayer.conf`), and adds
`npm run check:connection -- ws://HOST:PORT`, which reports `101` when consoles can
connect, `403` for a server without that fix, and `200` for a proxy that does not
forward upgrades. Until the hosted server runs that fix, consoles cannot use the
public fallback; once Apache forwards upgrades, the fallback moves back to `wss://`.

| Check | Result |
|---|---|
| `tests/test_multiplayer_wire.py` | Passes: the host wire test with the typed-address and port helpers, and source checks that a saved address is used alone, the discovery task ends itself and restarts on a lost connection, a search marks the server LOCAL, and Wi-Fi coming up starts a search |
| `tests/test_setup_takeover.py` | Passes with the screen's three input loops |
| `python3 -m prg32 esp32c6 build -p breadboard2` (full) | Builds; `sdkconfig` has no compiled address, the public fallback `ws://193.205.230.7:8099`, mDNS on; `prg32_cart_exec` stays at 0x408023a0 |
| `python3 -m prg32 qemu build` (full) | Builds, with no compiled address |
| `python3 -m prg32 test` and the CI checks | Pass; the QEMU smoke test wrote `cart0` of its own flash image only |

Not exercised: the board was not flashed, so the search on Wi-Fi, SEARCH LOCAL
NETWORK, the port step on screen, and a saved address left alone by discovery are
checked only by building, the source checks, and reading the code.
