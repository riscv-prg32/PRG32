# PRG32 MicroSD Card Interface (Under Development)

> [!IMPORTANT]
> **Status: Planned / Not Yet Implemented**
> 
> MicroSD card software drivers and filesystem mounting are **not yet implemented** in the resident PRG32 firmware.
> The SPI Chip Select (CS) GPIO has been reserved in hardware pin profiles (e.g. `prototype2`) and Kconfig to facilitate hardware preparation and future compatibility.

## Hardware Pin Allocation

In `prototype2` (`profiles/sdkconfig.pins.prototype2`), the MicroSD card interface is designed to share the high-speed SPI bus with the ILI9341 display while using a dedicated Chip Select (CS) line:

| Signal | ESP32-C6 GPIO | Notes |
|---|---|---|
| **SD_CS** | **GPIO9** | Dedicated SPI Chip Select for SD card (`CONFIG_PRG32_PIN_SDCARD_CS`) |
| **MOSI** | **GPIO7** | Shared with ILI9341 display MOSI |
| **MISO** | **GPIO2** | Shared with ILI9341 display MISO (Touch DO) |
| **SCLK** | **GPIO6** | Shared with ILI9341 display SCLK |
| **VCC** | **3V3** | 3.3V Logic Supply |
| **GND** | **GND** | Common Ground |

In `prototype1` (`profiles/sdkconfig.pins.prototype1`), `CONFIG_PRG32_PIN_SDCARD_CS` defaults to `-1` (unassigned), because GPIO9 was assigned to the LCD hardware reset line.

## Planned Software Architecture

When implemented in a future release:
1. **SPI Master Bus Sharing**: An SPI bus transaction arbiter will share the SPI peripheral between the ILI9341 display and the SD card driver (`esp_vfs_fat_sdspi_mount`).
2. **File Storage**: The SD card will be mounted at `/sdcard` using ESP-IDF FATFS/VFS.
3. **Cartridge Loading**: Games and asset files will be loadable directly from `.prg32` ROM packages placed on the MicroSD card.
4. **Save Data**: Non-volatile high scores and game save files will be persistable to the SD card filesystem.

## Current Firmware Behavior

- `CONFIG_PRG32_PIN_SDCARD_CS` can be set in `menuconfig` or profile fragment files.
- The compile-time macro `PRG32_PIN_SDCARD_CS` is exposed in `main/prg32_config.h`.
- The firmware does not initialize SD SPI transactions or mount any filesystem on boot.
