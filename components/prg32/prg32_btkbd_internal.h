#ifndef PRG32_BTKBD_INTERNAL_H
#define PRG32_BTKBD_INTERNAL_H

/*
 * Firmware-private interface of the PRG32 Bluetooth keyboard service.
 *
 * The service is split in two layers so that students can read each part on
 * its own:
 *
 *   prg32_btkbd.c      keyboard logic: HID boot reports -> key events, the
 *                      key queue read by cartridges, typematic repeat, and
 *                      the keyboard-to-joystick mapping stored in NVS.
 *   prg32_btkbd_ble.c  radio transport: NimBLE scanning, pairing/bonding,
 *                      GATT discovery of the HID service, and notifications.
 *
 * Nothing in this header is part of the cartridge ABI. Cartridges use only the
 * prg32_btkbd_* functions declared in prg32.h.
 */

#include <stddef.h>
#include <stdint.h>

#include "prg32.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One mapping row per PRG32 button bit 0..6: LEFT, RIGHT, UP, DOWN, A, B,
 * SELECT. Every button accepts a primary and an alternate HID usage; usage 0
 * means "no key". */
#define PRG32_BTKBD_BUTTON_COUNT 7
#define PRG32_BTKBD_KEYS_PER_BUTTON 2

typedef struct {
  uint8_t usage[PRG32_BTKBD_BUTTON_COUNT][PRG32_BTKBD_KEYS_PER_BUTTON];
} prg32_btkbd_map_t;

/* One entry of the internal key-event queue. `code` is the translated
 * PRG32_KEY_* / ASCII value (0 for keys without a character, such as a
 * modifier alone); `usage` is the raw HID usage used by the mapping editor. */
typedef struct {
  uint16_t code;
  uint8_t usage;
  uint8_t modifiers;
} prg32_btkbd_event_t;

/* One advertising HID device seen while scanning. */
#define PRG32_BTKBD_NAME_LEN 24
typedef struct {
  uint8_t addr_type;
  uint8_t addr[6];
  int8_t rssi;
  uint16_t appearance;
  char name[PRG32_BTKBD_NAME_LEN];
} prg32_btkbd_scan_entry_t;

/* ---- keyboard logic (prg32_btkbd.c) ---- */

/* Load the mapping from NVS and start the transport. Call after nvs_flash_init. */
void prg32_btkbd_init(void);

/* PRG32_BTN_* mask produced by the held keyboard keys, or 0 when the mapping
 * is disabled in setup or by the running cartridge. */
uint32_t prg32_btkbd_buttons(void);

/* Pop the next event, including repeat events. Returns 1 when `out` is valid. */
int prg32_btkbd_read_event(prg32_btkbd_event_t *out);

/* Transport callbacks: a standard 8-byte boot keyboard input report
 * (modifiers, reserved, six key usages) and a lost link. */
void prg32_btkbd_on_boot_report(const uint8_t *report, size_t length);
void prg32_btkbd_on_disconnect(void);

/* Console path used by QEMU: queue one already translated key code and keep
 * its HID usage "held" for a short time so games can poll key_down. */
void prg32_btkbd_inject_key(int code, uint8_t usage);

/* Persistent setup option "MAP TO CONTROLS". */
int prg32_btkbd_mapping_enabled(void);
void prg32_btkbd_mapping_set_enabled(int enabled);

/* Temporary per-cartridge override set through prg32_btkbd_set_mapping(). */
int prg32_btkbd_session_mapping_enabled(void);
void prg32_btkbd_session_reset(void);

void prg32_btkbd_map_get(prg32_btkbd_map_t *out);
void prg32_btkbd_map_set(const prg32_btkbd_map_t *map);
void prg32_btkbd_map_reset(void);

/* Short printable name of a HID usage for setup screens ("LEFT", "Z", ...). */
const char *prg32_btkbd_usage_name(uint8_t usage, char *buffer, size_t capacity);
const char *prg32_btkbd_state_name(uint32_t state);

/* ---- radio transport (prg32_btkbd_ble.c) ---- */

int prg32_btkbd_ble_init(void);
int prg32_btkbd_ble_available(void);
uint32_t prg32_btkbd_ble_state(void);
int prg32_btkbd_ble_scan_start(uint32_t duration_ms);
void prg32_btkbd_ble_scan_stop(void);
int prg32_btkbd_ble_scan_count(void);
int prg32_btkbd_ble_scan_get(int index, prg32_btkbd_scan_entry_t *out);
int prg32_btkbd_ble_connect(const prg32_btkbd_scan_entry_t *entry);
void prg32_btkbd_ble_forget(void);
int prg32_btkbd_ble_has_peer(void);
/* Six-digit passkey to type on the keyboard during pairing, or -1. */
int32_t prg32_btkbd_ble_passkey(void);
const char *prg32_btkbd_ble_last_error(void);
int prg32_btkbd_ble_device_name(char *buffer, size_t capacity);

/* ---- setup screen (prg32_setup_btkbd.c) ---- */

void prg32_setup_btkbd_run(void);

#ifdef __cplusplus
}
#endif

#endif
