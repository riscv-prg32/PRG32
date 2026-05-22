# Wi-Fi Setup Demo

This demo shows the framework-level Wi-Fi setup mode.

- The resident firmware enters setup after the startup splash when A+B are held,
  or automatically when no cartridge is stored.
- Custom builds can force setup on every boot with `PRG32_BOOT_SETUP_MODE`.
- Or run this demo and press SELECT or B to open the setup UI from a game-like
  program.
- Infrastructure mode scans available SSIDs; AP mode edits the AP credentials.
- The demo shows the active mode, current IP address, and current SSID.

Use entry prefix `wifi_setup_c`.
