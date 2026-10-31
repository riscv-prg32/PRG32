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
| `components/prg32/prg32_multiplayer.c` | The session: WebSocket lifecycle, reconnection, peer table, device identity, and QEMU's Ethernet |
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

Set the server with
[`prg32 multiplayer set-url`](../usage/network.md#multiplayer-server-address).
`wss://` verifies the server against the firmware's certificate bundle and uses a
larger task stack.

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

- `wss://` is implemented but has not been tested against a TLS server.
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
