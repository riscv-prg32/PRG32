# PRG32 QEMU Screen Emulator

PRG32 can run on a desktop with Espressif QEMU and show the 320x240 PRG32 screen in a virtual RGB framebuffer window. This is intended for students who want to compile and test graphics before flashing real ESP32-C6 hardware.

## Architecture Overview

While the physical PRG32 board uses an ESP32-C6 target with an ILI9341 SPI display, the QEMU emulator targets the **ESP32-C3** using a virtual RGB display backend (`CONFIG_PRG32_DISPLAY_QEMU_RGB`). This is because Espressif's maintained RISC-V QEMU graphics path requires the ESP32-C3.

- **Separate Build Environment**: QEMU uses a separate build directory (`build-qemu`) and defaults file (`sdkconfig.defaults.qemu`) to ensure physical board configurations remain untouched.
- **Unified ABI**: Both physical and QEMU targets share the same 32-bit RISC-V calling convention and PRG32 ABI. Only the display backend changes.
- **Screen Resolution**: Framework screens use the full 320x240 resolution, while game code draws into a centered 320x200 viewport.

## How It Works

- **Display Backends**: `components/prg32/Kconfig` switches between the physical ILI9341 SPI TFT (`CONFIG_PRG32_DISPLAY_ILI9341`) and the Espressif QEMU virtual RGB panel (`CONFIG_PRG32_DISPLAY_QEMU_RGB`).
- **Dependencies**: The `components/prg32/idf_component.yml` manifest pulls the `espressif/esp_lcd_qemu_rgb` component exclusively when the target is ESP32-C3.
- **Screenshots**: The QEMU backend exposes the same framebuffer snapshot API, meaning the `/api/screenshot.bmp` endpoint generates identical 320x240 BMPs as the real hardware.

## Prerequisites and Installation

### Supported Hosts
Espressif provides QEMU RISC-V packages for: macOS (Intel/Apple Silicon), Windows (x64), and Linux (x86_64, arm64).

### 1. Install ESP-IDF and QEMU
Read the Install & Setup section in the main [README](/README.md).

## Running QEMU

There are several ways to run the QEMU emulator, depending on your preferred workflow.

### Option A: Python Tooling (Recommended)

The unified Python tooling is the easiest way to build and launch QEMU on any platform:

```bash
python3 -m prg32 qemu build-and-run
```

### Option B: VS Code Tasks

If you are using the provided `PRG32.code-workspace`, run these tasks via the command palette:
1. `PRG32: qemu set target esp32c3`
2. `PRG32: qemu build`
3. `PRG32: qemu screen`

For debugger exercises, use two terminals:
1. Run `PRG32: qemu debug server`.
2. Run `PRG32: qemu gdb`.

## Using Cartridges in QEMU

QEMU uses the same `.prg32` game packages as the physical board.

1. **Build the QEMU Emulator** (if not already built):
   ```bash
   python3 -m prg32 qemu build
   ```
2. **Prepare a Cartridge**: Build a `.prg32` cartridge for QEMU. See [../software/cartridges.md](/docs/software/cartridges.md) for details.
3. **Upload the Cartridge**: Stage it into the emulator's flash image.
   ```bash
   python3 -m prg32 qemu upload build-qemu/asteroids.prg32
   ```
4. **Run QEMU**:
   ```bash
   python3 -m prg32 qemu run
   ```
   *(Note: You can also use `python3 -m prg32 qemu build-and-run` for convenience).*

## QEMU Audio (UART Redirection)

Because QEMU lacks native I2S emulation for the ESP32-C3 backend, PRG32 uses a custom **Credit-Based Flow Control** protocol to redirect the 22050Hz PCM audio stream over the virtual UART port (`tcp::4321`) to the host machine.

### Architecture Details

- **Background Player**: `launch_qemu.py` automatically spawns `tools/qemu_audio_player.py` in the background to interface with PyAudio on the host. It includes a process polling loop to gracefully terminate QEMU if the audio player fails to start.
- **Real-Time Pacing (Credit-Based Flow Control)**: QEMU instantly fast-forwards virtual time when the CPU is idle. To prevent audio from generating faster than the host can play it, the firmware's `audio_task` blocks on `uart_read_bytes` after sending each 20ms audio chunk. The Python script plays the audio natively (taking exactly 20ms) and sends a 1-byte `ACK` token back. This turns the host's physical audio hardware clock into the pacing mechanism for the entire QEMU virtual machine, guaranteeing flawless 1.0x real-time synchronization.
- **Network Optimizations**: Sending 1-byte ACKs and 882-byte audio chunks frequently triggers Nagle's Algorithm on modern OS network stacks, causing artificial 200ms batching delays. The `nodelay` flag is applied to QEMU's `-serial` arguments and `socket.TCP_NODELAY` is used in the Python listener to ensure instant transmission and zero network-induced stutter.
## Input and Controls

QEMU disables physical GPIO buttons and the buzzer, enabling a small UART-console keyboard mapper for player 1 input instead. 

> [!IMPORTANT]
> Because input is read from the UART console, you must ensure your **terminal window** running QEMU is the active window to use the keyboard, *not* the graphical QEMU screen itself.

| PRG32 Input | QEMU Key(s) |
| --- | --- |
| D-Pad (Up, Down, Left, Right) | Arrow keys or `W`, `S`, `A`, `D` |
| SELECT | `Enter` or `Space` |
| A button | `J` or `Z` |
| B button | `K`, `X`, `Backspace`, or `Esc` |

**Diagnostic Input Injection**:
Framework code can call `prg32_diag_set_input_state()` to inject player-1 inputs in QEMU-oriented tests. 

**Multiplayer API**:
The multiplayer API works in QEMU without actual Wi-Fi. Calling `prg32_multiplayer_join()` succeeds locally, `prg32_multiplayer_available()` returns true, and peer snapshots default to empty.


## Troubleshooting

Please refer to the [Troubleshooting Guide](troubleshooting.md#qemu-emulator-issues) for solutions to common QEMU issues.

## References

- [ESP-IDF QEMU Emulator for ESP32-C3][esp-idf-qemu]
- [Espressif `esp_lcd_qemu_rgb` component](https://components.espressif.com/components/espressif/esp_lcd_qemu_rgb)
- [IDF Component Manager manifest reference][idf-component-manifest]

[esp-idf-qemu]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/tools/qemu.html
[idf-component-manifest]: https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html
