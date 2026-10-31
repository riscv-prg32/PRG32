# Lab 05 - Scores and Multiplayer

## Goal

Use the score API and the PRG32 multiplayer service.

## Steps

1. Clone and run the classroom score server in one terminal:

```bash
git clone https://github.com/riscv-prg32/ScoreServer.git
cd ScoreServer
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python3 app.py
```

2. Test it from a terminal:

```bash
curl -X POST http://localhost:5000/api/scores \
  -H 'Content-Type: application/json' \
  -d '{"game":"pong","player":"Ada","score":42}'
```

3. Enable Wi-Fi scores in `idf.py menuconfig` (`PRG32 Firmware Features -> Enable Wi-Fi Scores API`) if using board networking.
4. Submit a local score with `prg32_score_submit`.
5. Start the multiplayer relay in another terminal:

```bash
git clone https://github.com/riscv-prg32/PRG32-Multiplayer-Server.git
cd PRG32-Multiplayer-Server
npm install
npm start
```

6. Point the firmware at the server host, as described in
   [Multiplayer server address](../../usage/network.md#multiplayer-server-address).
7. In a small C cartridge, call:

```c
prg32_multiplayer_join("lab05-demo", PRG32_MP_FLAG_ENABLE);
prg32_multiplayer_set_input(prg32_input_read_player(1));
prg32_multiplayer_set_local_state(x, y, 0, 0);
prg32_multiplayer_tick();
```

8. Verify that `prg32_multiplayer_available()` returns true, and that
   `prg32_multiplayer_local_player_id()` becomes nonzero shortly after the join.
9. Query the cartridge runtime:

```bash
curl http://192.168.4.1/api/runtime
```

10. Build and upload one `.prg32` cartridge with:

```bash
python3 -m prg32 cartridge build examples/games/pong/c/game.c \
  --out build/pong.prg32 --entry-prefix pong_c --multiplayer
```

## Checkpoint

Show one score in `/api/scores`, one multiplayer join using a cartridge
signature, and at least one uploaded cartridge slot listed by `/api/games`.

Both the physical ESP32-C6 board and QEMU join real rooms. Run two QEMU instances
([Running several emulators](../../usage/qemu.md#running-several-emulators)) to
see a second player without a second board.

## Reflection

Why does the multiplayer room use a cartridge signature instead of a display
name typed by the player?
