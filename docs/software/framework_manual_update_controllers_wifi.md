# Framework Update: Input, Wi-Fi Scores, and Multiplayer

This update adds three services to PRG32:

1. `prg32_controller.c`: reads the local GPIO joystick and QEMU keyboard mapper.
2. `prg32_wifi.c`, `prg32_http_scores.c`, and `prg32_http_games.c`: expose a
   small Wi-Fi + HTTP API for game upload/runtime info and score records.
3. `prg32_multiplayer.c`: shares cartridge player snapshots over Wi-Fi station
   mode and WebSocket.

## New C functions

```c
uint32_t prg32_controller_read(void);
const char *prg32_controller_name(uint32_t bit);
uint32_t prg32_input_read_menu(void);
void prg32_wifi_scores_init(void);
prg32_wifi_mode_t prg32_wifi_current_mode(void);
const char *prg32_wifi_current_ip(void);
const char *prg32_wifi_current_ssid(void);
void prg32_scores_api_start(void);
int prg32_score_submit(const char *game, const char *player, uint32_t score);
void prg32_multiplayer_init(void);
bool prg32_multiplayer_available(void);
int prg32_multiplayer_join(const char *signature, uint32_t flags);
```

`PRG32_BTN_SELECT` is the preferred classroom name for the select/start button.
Use `prg32_input_read_menu()` for setup screens so menus and games share the
same local joystick controls.

## Assembly ABI examples

Read controller:

```asm
call prg32_input_read
andi t0, a0, PRG32_BTN_A
bnez t0, fire
```

Submit score:

```asm
la a0, game_name
la a1, player_name
li a2, 999
call prg32_score_submit
```
