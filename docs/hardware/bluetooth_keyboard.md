# Bluetooth Keyboard

PRG32 can pair with a wireless keyboard. The keyboard has two uses:

- **typing**: cartridges read characters and special keys through the
  `prg32_btkbd_*` [cartridge ABI](../software/abi.md#bluetooth-keyboard-abi-calls),
  for example the [PRG32 C interpreter](../../cartridges/c-language/README.md);
- **playing**: keys can be mapped to the joystick buttons, so every existing
  game and the setup menus also work from the keyboard.

No wiring is needed. The keyboard is optional: see the KBD entry in the
[bill of materials](where_to_buy.md) for buying advice.

## Which keyboards work

The ESP32-C6 radio supports **Bluetooth Low Energy (LE) only**, not Bluetooth
Classic. PRG32 therefore talks to keyboards that implement the *HID over GATT
Profile* (HOGP), which is what "Bluetooth LE", "BLE" or "Bluetooth 5.x LE"
keyboards use. Classic-only keyboards and keyboards with a 2.4 GHz USB dongle
cannot be used. Letters are translated with the US layout.

## Pairing

1. Enter setup (hold A+B at reset, or boot without cartridges) and open
   **SETUP > BLUETOOTH KEYBOARD > PAIR NEW KEYBOARD**.
2. Put the keyboard in pairing mode (see its manual; often a long press on a
   Bluetooth/channel key).
3. Select the keyboard in the list (names appear a few seconds after the
   address) and press A or SELECT.
4. If PRG32 shows a six-digit number, type it on the keyboard and press ENTER.
   Keyboards without this step pair directly ("just works").
5. **CONNECTED** confirms pairing. The keys are bonded in NVS: after a reset,
   or when the keyboard wakes up, PRG32 reconnects automatically.

The **TEST** line of the keyboard screen echoes typed characters.
**FORGET KEYBOARD** deletes the bond; pair again to use another keyboard.

## Keyboard as joystick

With **MAP TO CONTROLS: ON** (the default), held keys set the same
`PRG32_BTN_*` bits as the physical joystick:

| Button | Primary key | Alternate key |
|---|---|---|
| LEFT | LEFT arrow | A |
| RIGHT | RIGHT arrow | D |
| UP | UP arrow | W |
| DOWN | DOWN arrow | S |
| A | Z | J |
| B | X | ESC |
| SELECT | ENTER | SPACE |

These defaults mirror the [QEMU console keys](../usage/qemu.md). **CUSTOMIZE
MAPPING** assigns any key, including CTRL, SHIFT and ALT, to each button:
select a button, press the new primary key, then the alternate key (BACKSPACE
leaves the alternate empty; the joystick B button cancels). **RESET DEFAULT
MAPPING** restores the table above. The mapping and the ON/OFF option are
stored in NVS (`prg32` namespace, keys `kbd_map` and `kbd_map_on`).

Remember that A+B+DOWN restarts PRG32 on any input device, so the default keys
Z+X+S (or their arrows) pressed together reset the console.

## Reading keys in a cartridge

```c
void notes_update(void) {
    int key;
    while ((key = prg32_btkbd_read_key()) != 0) {
        if (key == PRG32_KEY_ENTER) { /* ... */ }
        else if (key >= 32 && key < 127) { /* printable ASCII */ }
        else if (key == PRG32_KEY_LEFT) { /* special keys >= 0x100 */ }
    }
    if (prg32_btkbd_key_down(PRG32_HID_KEY_LETTER('Q'))) { /* held */ }
}
```

`prg32_btkbd_read_key()` never blocks. A text cartridge should call
`prg32_btkbd_set_mapping(0)` in its init function so typed letters do not also
press joystick buttons; the firmware restores the mapping when the next
cartridge starts. The full contract is in the [ABI guide](../software/abi.md#bluetooth-keyboard-abi-calls).

On QEMU there is no radio: the terminal running the emulator is the keyboard
(`prg32_btkbd_state()` returns `PRG32_BTKBD_STATE_CONSOLE`), so the same
cartridge can be tested on the desktop.

## How it works

The service is split in two readable layers inside `components/prg32/`:

| File | Role |
|---|---|
| `prg32_btkbd_ble.c` | NimBLE central: scan for the HID service (UUID `0x1812`) or keyboard appearance `0x03C1`, connect, pair/bond, discover the HID characteristics and their CCCDs, select the *boot protocol* and enable notifications |
| `prg32_btkbd.c` | keyboard logic: compares consecutive 8-byte boot reports (modifiers, reserved, six key usages) to find new key presses, translates HID usages to ASCII/`PRG32_KEY_*`, queues them, repeats held keys after 500 ms, and turns held keys into joystick bits |
| `prg32_setup_btkbd.c` | the setup screens described above |
| `prg32_qemu_input.c` | on QEMU, feeds console characters and escape sequences (arrows, F1–F12, HOME, END, DELETE, PAGE UP/DOWN) into the same key queue |

The boot protocol gives every HOGP keyboard the same fixed report layout, so
no HID report-descriptor parser is needed. NimBLE callbacks run in the NimBLE
host task; a small `prg32_btkbd` task performs scan/connect/forget requests
from the setup screens and reconnects the bonded keyboard every few seconds
while it is away. A FreeRTOS critical section protects the key queue shared
with the cartridge running in the main task.

## Configuration and cost

`sdkconfig.defaults` enables the NimBLE host in central-only mode with one
connection and bonds stored in NVS, and sets `CONFIG_PRG32_BT_KEYBOARD=y`
(Kconfig *PRG32 framework > Enable Bluetooth LE keyboard host*). Measured on
the ESP32-C6 build, Bluetooth adds about 300 KB of flash (15% of the 2 MB app
partition remains free) and 28 KB of static RAM, plus the NimBLE heap at run
time. QEMU builds (`sdkconfig.defaults.qemu`) keep Bluetooth disabled; the
ABI is still present and reports the console keyboard.

## Troubleshooting

| Symptom | Check |
|---|---|
| Keyboard not in the list | pairing mode active; keyboard supports Bluetooth LE; within a few metres |
| `PAIRING FAILED, RETRY` | the keyboard was paired with another host: put it in pairing mode again and pair; PRG32 has already deleted its stale bond |
| `NOT A KEYBOARD` | the device offers no keyboard report (for example a mouse or remote) |
| Keys do not move games | **MAP TO CONTROLS** is ON; the running cartridge did not disable the mapping |
| Status stays `NOT CONNECTED` after reset | wake the keyboard with a key press; it reconnects within a few seconds |
