#ifndef PRG32_CONFIG_H
#define PRG32_CONFIG_H

#include "sdkconfig.h"

/* Example game identifiers used by lab builds that opt into examples/games. */
#define PRG32_GAME_PONG_ASCII          1
#define PRG32_GAME_PONG_GRAPHICS       2
#define PRG32_GAME_BREAKOUT_ASCII      3
#define PRG32_GAME_BREAKOUT_GRAPHICS   4
#define PRG32_GAME_INVADERS_ASCII      5
#define PRG32_GAME_INVADERS_GRAPHICS   6
#define PRG32_GAME_PACMAN_ASCII        7
#define PRG32_GAME_PACMAN_GRAPHICS     8
#define PRG32_GAME_ASTEROIDS_ASCII     9
#define PRG32_GAME_ASTEROIDS_GRAPHICS  10
#define PRG32_GAME_TETRIS_ASCII        11
#define PRG32_GAME_TETRIS_GRAPHICS     12
#define PRG32_GAME_TETRIS_C            13
#define PRG32_GAME_PLATFORMER_ASCII    14
#define PRG32_GAME_PLATFORMER_GRAPHICS 15
#define PRG32_GAME_PLATFORMER_C        16
#define PRG32_GAME_PONG_C              17
#define PRG32_GAME_BREAKOUT_C          18
#define PRG32_GAME_INVADERS_C          19
#define PRG32_GAME_PACMAN_C            20
#define PRG32_GAME_ASTEROIDS_C         21
#define PRG32_GAME_RAYCASTER_ASCII     22
#define PRG32_GAME_RAYCASTER_GRAPHICS  23
#define PRG32_GAME_RAYCASTER_C         24
#define PRG32_GAME_WING_ASCII          25
#define PRG32_GAME_WING_GRAPHICS       26
#define PRG32_GAME_WING_C              27
#define PRG32_GAME_FROGGER_ASCII       28
#define PRG32_GAME_FROGGER_GRAPHICS    29
#define PRG32_GAME_FROGGER_C           30
#define PRG32_GAME_PRG32_BOUNCE_ASCII    31
#define PRG32_GAME_PRG32_BOUNCE_GRAPHICS 32
#define PRG32_GAME_PRG32_BOUNCE_C        33

/* Runtime console mode. */
#define PRG32_MODE_UART_ONLY       0
#define PRG32_MODE_LCD_ONLY        1
#define PRG32_MODE_UART_LCD_MIRROR 2

#define PRG32_DEFAULT_MODE PRG32_MODE_UART_LCD_MIRROR

#ifndef PRG32_DEBUG
#define PRG32_DEBUG 0
#endif

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
/* QEMU screen builds do not touch physical board pins. */
#define PRG32_PIN_LCD_MOSI   -1
#define PRG32_PIN_LCD_MISO   -1
#define PRG32_PIN_LCD_SCLK   -1
#define PRG32_PIN_LCD_CS     -1
#define PRG32_PIN_LCD_DC     -1
#define PRG32_PIN_LCD_RST    -1
#define PRG32_PIN_LCD_BL     -1

#define PRG32_PIN_BTN_LEFT   -1
#define PRG32_PIN_BTN_RIGHT  -1
#define PRG32_PIN_BTN_UP     -1
#define PRG32_PIN_BTN_DOWN   -1
#define PRG32_PIN_BTN_A      -1
#define PRG32_PIN_BTN_B      -1
#define PRG32_PIN_BTN_START  -1
#define PRG32_PIN_SETUP      -1
#define PRG32_PIN_BUZZER     -1
#define PRG32_BOOT_DIAGNOSTIC_DELAY_MS 0

#define PRG32_PIN_RGB_LED -1

#define PRG32_GAME_UPLOAD_ENABLE 0
#else
/*
 * ESP32-C6 breadboard wiring for the physical ILI9341 build.
 * These pins match the classroom display harness tested with Arduino/Adafruit.
 */
#define PRG32_PIN_LCD_MOSI   7
#define PRG32_PIN_LCD_MISO   2
#define PRG32_PIN_LCD_SCLK   6
#define PRG32_PIN_LCD_CS     10
#define PRG32_PIN_LCD_DC     1
#define PRG32_PIN_LCD_RST    0
#define PRG32_PIN_LCD_BL     5

#define PRG32_LCD_SPI_CLOCK_HZ 32000000
#define PRG32_LCD_BACKLIGHT_ACTIVE_LEVEL 1
#define PRG32_LCD_BOOT_TEST_MS 0
#define PRG32_LCD_SOFT_SPI 0
#define PRG32_BOOT_SIGNAL_ENABLE 0
#define PRG32_BOOT_DIAGNOSTIC_DELAY_MS 0

#define PRG32_PIN_BTN_UP     3
#define PRG32_PIN_BTN_DOWN   13
#define PRG32_PIN_BTN_LEFT   18
#define PRG32_PIN_BTN_RIGHT  19
#define PRG32_PIN_BTN_START  20
#define PRG32_PIN_BTN_A      21
#define PRG32_PIN_BTN_B      22

#define PRG32_PIN_SETUP      14

#define PRG32_PIN_BUZZER     -1

/*
 * Many ESP32-C6 boards route the onboard addressable RGB LED to GPIO8. The
 * reference PRG32 ILI9341 harness also uses GPIO8 for LCD D/C, so the RGB LED
 * is disabled by default. Set this to the board LED GPIO only when that pin is
 * free in your hardware variant.
 */
#define PRG32_PIN_RGB_LED 8
#endif

/*
 * Uploadable game cartridges.
 *
 * Physical-board builds enable a small SoftAP + HTTP API by default so the
 * firmware can be flashed once and games can be replaced from a host tool.
 * QEMU builds keep Wi-Fi disabled and use python3 -m prg32 to patch the
 * emulator flash image.
 */
#ifndef PRG32_GAME_UPLOAD_ENABLE
#define PRG32_GAME_UPLOAD_ENABLE 1
#endif

/* Optional Wi-Fi score REST API. Fill credentials before flashing. */
#define PRG32_WIFI_SCORES_ENABLE 0

#if __has_include("prg32_env.h")
#include "prg32_env.h"
#endif

#ifndef PRG32_WIFI_SSID
#define PRG32_WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef PRG32_WIFI_PASSWORD
#define PRG32_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#define PRG32_SCORE_MAX 16
#define PRG32_IDLE_HEARTBEAT_MS 5000

/* Optional cartridge multiplayer service over Wi-Fi STA + WebSocket. */
#define PRG32_MULTIPLAYER_ENABLE 1
#define PRG32_MULTIPLAYER_SERVER_URL "ws://192.168.4.2:8081"
#define PRG32_MULTIPLAYER_SEND_PERIOD_MS 50
#define PRG32_MULTIPLAYER_PEER_TIMEOUT_MS 3000

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#define PRG32_MULTIPLAYER_TRANSPORT_ENABLE 0
#else
#define PRG32_MULTIPLAYER_TRANSPORT_ENABLE PRG32_MULTIPLAYER_ENABLE
#endif

#define PRG32_WIFI_STA_ENABLE (PRG32_WIFI_SCORES_ENABLE || PRG32_MULTIPLAYER_TRANSPORT_ENABLE)
#define PRG32_WIFI_AP_ENABLE PRG32_GAME_UPLOAD_ENABLE
#define PRG32_WIFI_ENABLE (PRG32_WIFI_STA_ENABLE || PRG32_WIFI_AP_ENABLE)
#define PRG32_BOOT_SETUP_MODE 1
#define PRG32_WIFI_AP_SSID "PRG32"
#define PRG32_WIFI_AP_PASSWORD "prg32game"
#define PRG32_WIFI_AP_CHANNEL 6
#define PRG32_WIFI_AP_MAX_CONN 4
#define PRG32_WIFI_COUNTRY_CODE "IT"
#define PRG32_WIFI_STA_LEGACY_PROTOCOLS 1

/* CartridgeStore integration constants. */
#define PRG32_STORE_URL_MAX_LEN        128
#define PRG32_STORE_SERVER_URL         "http://193.205.230.7:5080/"
#define PRG32_STORE_MDNS_SERVICE       "_prg32store"
#define PRG32_STORE_MDNS_PROTO         "_tcp"
#define PRG32_STORE_MDNS_TIMEOUT_MS    3000
#define PRG32_STORE_DEFAULT_PORT       5080
#define PRG32_STORE_CATALOG_MAX_BYTES  16384
#define PRG32_STORE_CHUNK_BYTES        4096
#define PRG32_STORE_DOWNLOAD_STACK     8192
#define PRG32_STORE_HTTP_TIMEOUT_MS    5000

/*
 * SELECT is the classroom-facing name; START remains a source-compatible alias.
 * GPIO14 remains the safe default for classroom wiring. Set
 * PRG32_ENABLE_SEPARATE_SELECT_PIN to 1 in a local build only when the
 * optional 7-button harness wires SELECT to GPIO20.
 */
#ifndef PRG32_ENABLE_SEPARATE_SELECT_PIN
#define PRG32_ENABLE_SEPARATE_SELECT_PIN 0
#endif

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#define PRG32_PIN_BTN_SELECT PRG32_PIN_BTN_START
#elif PRG32_ENABLE_SEPARATE_SELECT_PIN
#define PRG32_PIN_BTN_SELECT 20
#else
#define PRG32_PIN_BTN_SELECT PRG32_PIN_BTN_START
#endif

#endif
