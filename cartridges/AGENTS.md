# Cartridge Agent Instructions

- Each cartridge must be built as a portable cartridge.
- During debugging or bug fixing, changes must be limited to the affected
  cartridge's source code. Do not modify the firmware, especially its public
  ABIs, unless the user explicitly requests that work.
- If a firmware bug is found, describe the bug and ask the user to authorize a
  firmware fix before proceeding.
