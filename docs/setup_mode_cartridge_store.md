# Setup Mode Cartridge Store Integration

The downloadable game server is called the **Cartridge Store**. Its standalone
repository is `riscv-prg32/CartridgeStore`; it is intentionally not vendored
into this PRG32 firmware repository.

This document records the firmware-side integration contract. The current PRG32
repository includes the cartridge metadata format, host tooling, and setup-mode
pseudocode. A full embedded browser/downloader can be added once the classroom
network policy and memory budget are fixed for the target boards.

## Discovery

Preferred discovery order:

1. Previously saved Cartridge Store URL from NVS.
2. mDNS service `_prg32store._tcp.local`.
3. `GET /.well-known/prg32-store.json` on likely local hosts.
4. Manual URL entry with the joystick text input screen.
5. QR-code/manual URL fallback on constrained displays.

Well-known response:

```json
{
  "abi": "prg32-store-discovery-1.0",
  "name": "PRG32 Cartridge Store",
  "api": "http://host:port/api",
  "web": "http://host:port/",
  "version": "1.0.0"
}
```

## Store Client API

Firmware clients should prefer compact REST calls:

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/games` | list games, versions, and available architectures |
| `GET` | `/api/games/<id>` | fetch one game detail record |
| `GET` | `/api/games/<id>/icon` | fetch icon bytes |
| `GET` | `/api/games/<id>/screenshot` | fetch screenshot bytes if available |
| `GET` | `/api/games/<id>/colophon` | fetch compact colophon JSON |
| `GET` | `/api/games/<id>/download?architecture=esp32c6&version=1.0.0` | download `.prg32` |

The firmware should request `architecture=esp32c6`. QEMU host tools should
request `architecture=qemu`.

## Setup Menu Page

A complete setup page should provide:

- manual Cartridge Store URL entry;
- automatic discovery;
- connection test;
- browsing games after connection;
- viewing metadata and colophon;
- selecting `cart0` or `cart1`;
- downloading and installing the chosen game/version/architecture.

Firmware-side sketch:

```c
static void setup_cartridge_store_menu(void) {
    char store_url[96];
    load_or_enter_store_url(store_url, sizeof(store_url));

    if (discover_store_url(store_url, sizeof(store_url)) != 0) {
        prg32_text_input("STORE URL", store_url, sizeof(store_url));
    }

    if (test_store_connection(store_url) != 0) {
        show_setup_message("CARTRIGE STORE", "CONNECTION FAILED", PRG32_COLOR_RED, 1500);
        return;
    }

    prg32_store_game_t games[8];
    int count = fetch_store_games(store_url, games, 8, PRG32_CART_ARCH_ESP32C6);
    int game = pick_store_game(games, count);
    if (game < 0) {
        return;
    }

    show_metadata_and_colophon_preview(store_url, games[game].id);

    uint8_t slot = pick_cartridge_slot();
    stream_download_to_slot(
        store_url,
        games[game].id,
        games[game].version,
        PRG32_CART_ARCH_ESP32C6,
        slot);

    prg32_cart_select_slot(slot);
    show_cartridge_colophon_before_start(slot);
}
```

The download path should stream into the selected flash partition rather than
buffering a full monolithic cartridge in executable cartridge RAM. The loader
will still validate the legacy executable payload before launch, and the
metadata trailer stays after the code/AUDIO payload for old-tool compatibility.

## Activation Rule

The game colophon is shown after the cartridge is activated, before the player
starts a new play.

If no `COLO` block exists, keep legacy behavior. A minimal fallback screen may
show the metadata title, version, and developer when the `META` block is
available.
