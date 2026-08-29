# PRG32 Score API

PRG32 can expose a small REST API over Wi-Fi and can also submit records to the
standalone [ScoreServer](https://github.com/riscv-prg32/ScoreServer). This is
useful for competitions and for teaching the boundary between assembly, C, and
network services.

Games can use the score feature without Wi-Fi. The local firmware keeps the
five best persistent local scores for each game, includes an on-screen
player-name prompt, and provides a simple scoreboard screen that cartridges can
call directly.

## Enable Wi-Fi

Edit `main/prg32_config.h`:

```c
#define PRG32_WIFI_SCORES_ENABLE 1
#define PRG32_WIFI_SSID "your-network"
#define PRG32_WIFI_PASSWORD "your-password"
```

Then build and flash normally with ESP-IDF.

## API endpoints

### Get scores

```http
GET /api/scores
```

Response:

```json
[
  {"game":"pong","player":"Ada","score":42}
]
```

### Submit score

```http
POST /api/scores
Content-Type: application/json

{"game":"breakout","player":"Grace","score":1200}
```

Response:

```json
{"ok":true}
```

## Assembly usage

Assembly code can submit an explicit player name at the end of a game:

```asm
la a0, game_name      # const char *game
la a1, player_name    # const char *player
li a2, 1200           # uint32_t score
call prg32_score_submit
```

This is a clean ABI example: arguments in `a0`, `a1`, `a2`, return value in `a0`.

For a cartridge that lets the runtime remember the current player name, call
the prompt once from C or assembly glue before play starts:

```c
prg32_score_player_prompt();
```

Then submit the score with the remembered player:

```c
prg32_score_submit_current_player("breakout", score);
```

To show the local scoreboard from a game:

```c
prg32_scoreboard_show("breakout", "BREAKOUT SCORES");
```

The lower-level helpers `prg32_score_player_get`,
`prg32_score_player_set`, `prg32_score_count`, and `prg32_score_get` are also
available to cartridges that want to draw their own scoreboard UI.

When a Cartridge Store URL has been saved in setup or compiled with
`CONFIG_PRG32_STORE_URL`, local submissions are also marked for remote upload.
`prg32_score_sync_remote()` retries pending local top-five records without
requiring the game to know whether a store has been configured yet. If no store
URL exists, the local scoreboard still works normally.

## Remote classroom server

Clone and run the standalone Flask + SQLite server:

```bash
git clone https://github.com/riscv-prg32/ScoreServer.git
cd ScoreServer
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python3 app.py
```

Then submit from a host:

```bash
curl -X POST http://localhost:5000/api/scores \
  -H 'Content-Type: application/json' \
  -d '{"game":"pong","player":"Ada","score":42}'
```

Firmware C code can submit remotely with:

```c
prg32_score_submit_remote("http://192.168.1.20:5000",
                          "pong",
                          "Ada",
                          42);
```

## Current implementation

The board-local implementation stores the top five scores per game in a
dedicated local NVS partition. Local scores survive shutdown, reboot, and
cartridge replacement. Pending records remain marked for retry until
`prg32_score_sync_remote()` can submit them to the configured Cartridge Store
score API. The standalone ScoreServer and Cartridge Store persist uploaded
records in SQLite.

The same board HTTP server also hosts the cartridge upload API when
`PRG32_GAME_UPLOAD_ENABLE` is enabled. See `docs/cartridges.md`.
