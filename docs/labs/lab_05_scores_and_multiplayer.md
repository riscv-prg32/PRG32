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

3. Enable Wi-Fi scores in `main/prg32_config.h` if using board networking.
4. Submit a local score with `prg32_score_submit`.
5. Start the multiplayer relay in another terminal:

```bash
git clone https://github.com/riscv-prg32/MultiplayerServer.git
cd MultiplayerServer
npm install
npm start
```

6. Configure `PRG32_MULTIPLAYER_SERVER_URL` in `main/prg32_config.h` to point at
   the server host.
7. In a small C cartridge, call:

```c
prg32_multiplayer_join("lab05-demo", PRG32_MP_FLAG_ENABLE);
prg32_multiplayer_set_input(prg32_input_read_player(1));
prg32_multiplayer_set_local_state(x, y, 0, 0);
prg32_multiplayer_tick();
```

8. Verify that `prg32_multiplayer_available()` returns true. On QEMU,
   `prg32_multiplayer_join()` succeeds locally even without real Wi-Fi.
9. Query the cartridge runtime:

```bash
curl http://192.168.4.1/api/runtime
```

10. Build and upload one `.prg32` cartridge with:

```bash
python3 -m prg32 build examples/games/pong/c/game.c \
  --out build/pong.prg32 --entry-prefix pong_c --multiplayer
```

## Checkpoint

Show one score in `/api/scores`, one multiplayer join using a cartridge
signature, and at least one uploaded cartridge slot listed by `/api/games`.

Use the physical ESP32-C6 board for Wi-Fi station mode and WebSocket testing.
QEMU can still run the cartridge and exercise the offline multiplayer API.

## Reflection

Why does the multiplayer room use a cartridge signature instead of a display
name typed by the player?
