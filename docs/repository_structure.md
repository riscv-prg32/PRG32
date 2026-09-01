# Repository Structure

This document outlines the high-level layout of the PRG32 repository. 

> [!IMPORTANT]
> This information is VERY IMPORTANT AND SHOULD ALWAYS BE UPDATED AFTER EVERY CHANGE.

## Complete Repository Layout

```text
.
|-- components/
|   |-- prg32/                      ESP-IDF component implementing the core PRG32 API (graphics, input, network, cartridge loader)
|   `-- prg32_audio/                ESP-IDF component for audio processing, synthesis, and trackers
|-- docs/                           Manuals, tutorials, hardware docs, and labs
|   |-- agents/                     Guidelines for autonomous coding agents
|   |-- cartridge_store/            CartridgeStore API and ScoreServer API documentation
|   |-- hardware/                   Hardware integration guides (displays, controllers, memory, etc.)
|   |-- learn/                      Classroom tutorials, lab handouts, and educational material
|   |-- measurement/                Scientific measurement and metrics API documentation
|   |-- software/                   Firmware manuals, C framework details, and ABI definitions
|   |-- tools/                      Guides for Python tooling and conversion utilities
|   `-- usage/                      Getting started, tooling, QEMU, and troubleshooting guides
|-- examples/
|   |-- features/                   Focused rendering and firmware feature demos (audio, sprites, dual playfield)
|   `-- games/                      Full game examples in RISC-V assembly and C (asteroids, breakout, pacman, etc.)
|   |-- pcb/                        Hardware reference designs (Fritzing PCB projects)
|-- main/                           Minimal resident firmware app and board configuration
|   |-- main.c                      Resident runtime application
|   `-- prg32_config.h              Board pins, feature flags, and compile-time constants
|-- managed_components/             External component dependencies managed by ESP-IDF (mDNS, WebSocket client)
|-- prg32/                          Python module containing the core CLI tooling
|   |-- abi/                        Cartridge ABI generator and JSON specification
|   |-- cartridge/                  Cartridge packaging scripts
|   |-- esp32c6/                    Hardware flashing, upload, and verification commands
|   |-- qemu/                       Emulator launch, staging, and automated smoke testing
|   |-- store/                      CartridgeStore integration (publishing, formatting, metadata)
|   `-- utilities/                  Common utility functions for logging, environment checks, and partitions
|-- scripts/                        Shell/PowerShell scripts for common CI and local workflows (QEMU, flashing)
|-- sdkconfig*                      Default configuration files for ESP32-C6 hardware and QEMU emulator builds
|-- tests/                          Host-side unit tests for Python tooling and documentation validation
|-- tools/                          Standalone developer Python scripts (image conversion, audio packing, metrics, etc.)
|-- .vscode/                        Student-ready VS Code tasks and debug configurations
|-- AGENTS.md                       Rules and guidelines for autonomous coding agents
|-- CMakeLists.txt                  Top-level CMake project definition
|-- CONTRIBUTING.md                 Guidelines for human contributors
|-- CONTRIBUTORS.md                 Academic contributor metadata
|-- partitions_prg32.csv            Partition table defining resident firmware layout and cartridge slots
`-- PRG32.code-workspace            VS Code workspace configuration
```
