# Repository Structure

This document outlines the high-level layout of the PRG32 repository. 

> [!IMPORTANT]
> This information is VERY IMPORTANT AND SHOULD ALWAYS BE UPDATED AFTER EVERY CHANGE.

## Complete Repository Layout

```text
.
|-- .github/workflows/ci.yml        ESP32-C6 and QEMU firmware builds, host checks, and store-ready cartridge CI/CD artifacts
|-- cartridges/                     In-tree portable cartridges and store-ready bundles
|   |-- audiotest/                  Source-only audio pan diagnostic cartridge
|   |-- bachdemo/                   Eight-voice procedural Bach audio showcase
|   |-- blackjack/                  Casino blackjack game and rules tests
|   |-- devicedemo/                 Hardware/runtime feature showcase
|   |-- performancetest/            RGB565/full-indexed public-ABI measurement reference
|   `-- poing/                      Indexed procedural graphics and application benchmark
|       `-- assets/                 Screenshots and 30-second audiovisual previews
|-- components/
|   |-- prg32/                      ESP-IDF component implementing the core PRG32 API (indexed/RGB565 graphics, input, network, eight-slot cartridge loader)
|   |   |-- prg32_cart.c            Cartridge slots and loader: execution window filled from a flash mapping, streamed-upload validation
|   |   |-- prg32_display_ili9341.c Physical display backend and reference landscape orientation
|   |   |-- prg32_led_internal.h    Firmware-only shared RGB/LCD boot-indicator helpers
|   |   |-- prg32_performance.c     Bounded performance collection and streamed JSON result serialization
|   |   |-- prg32_usb_protocol.c    Automatic framed USB/UART upload, run, performance, and Wi-Fi protocol with upload timeout, failed-slot erase, HELLO heap reporting, and whole-frame writes that console logs cannot split
|   |   |-- prg32_wifi.c            Wi-Fi modes, setup screens, the last four connected networks, and forgetting a stored network
|   |   |-- prg32_random.c          Uniform bounded random-number API
|   |   `-- Kconfig                 Firmware configuration options (menuconfig)
|   `-- prg32_audio/                ESP-IDF audio component; audio_synth.c holds the private SID-like oscillator, ADSR, noise, and filter core; audio_tracker.c runs tracker event timing
|-- docs/                           Manuals, tutorials, hardware docs, and labs
|   |-- agents/                     Guidelines for autonomous coding agents
|   |   |-- device_protocol_consistency.md  Rules for keeping firmware, SDK, web clients, and hardware docs aligned
|   |   `-- python_language_consistency.md  Rules for keeping the SDK and Construction Kit PRG32 Python compilers identical
|   |-- cartridge_store/            CartridgeStore API and ScoreServer API documentation
|   |-- hardware/                   Hardware integration guides (displays, controllers, memory, etc.)
|   |   |-- AGENTS.md               Hardware and purchasing-document maintenance rules
|   |   |-- images/                 Electrical schematic (Fritzing export) and breadboard prototype figures
|   |   |-- pcb/                    Fritzing PCB reference design, layout status, and fabrication notes
|   |   `-- where_to_buy.md         Central bill of materials and country-specific supplier links
|   |-- learn/                      Classroom tutorials, lab handouts, and educational material
|   |   `-- tutorial_python_game.md PRG32 Python cartridge tutorial
|   |-- performance_test.md         Canonical performance-test execution, interpretation, and extension guide
|   |-- measurement/                Scientific methodology, metrics schema, ABI, and validation records
|   |-- software/                   Firmware manuals, C framework details, and ABI definitions (Note: the framework manual is for cartridge users, not framework developers)
|   |   |-- python_language.md      PRG32 Python language, commands, and alignment with the Construction Kit
|   |   |-- usb_protocol.md         Versioned offline device-protocol wire format, transport rules, and SDK workflow
|   |   `-- web_usb_deployment.md   Web Serial deployment contract for the Construction-Kit and CartridgeStore
|   |-- tools/                      Guides for Python tooling and conversion utilities
|   `-- usage/                      Getting started, tooling, QEMU, and troubleshooting guides
|-- examples/
|   |-- features/                   Focused rendering and firmware feature demos (audio, sprites, dual playfield)
|   |   `-- random_number/          C and assembly bounded-random examples
|   `-- games/                      Full game examples in RISC-V assembly and C (asteroids, breakout, pacman, etc.)
|       `-- pong/python/game.py     PRG32 Python version of Pong
|   |-- pcb/                        Hardware reference designs (Fritzing PCB projects)
|-- main/                           Minimal resident firmware app and board configuration
|   |-- main.c                      Resident runtime application
|   `-- prg32_config.h              Board pins, feature flags, and compile-time constants
|-- managed_components/             External component dependencies managed by ESP-IDF (mDNS, WebSocket client)
|-- prg32/                          Python module containing the core CLI tooling
|   |-- abi/                        Cartridge ABI generator and JSON specification
|   |-- cartridge/                  Cartridge packaging scripts
|   |-- esp32c6/                    Hardware build, HTTP/USB cartridge upload, firmware flashing, verification, and USB Wi-Fi network commands
|   |-- python/                     PRG32 Python compiler shared with PRG32-Construction-Kit (game.py -> game IR -> C/assembly) and the `python` command
|   |-- qemu/                       Emulator launch, staging, and automated smoke testing
|   |-- usb_protocol.py             Shared framed device-protocol client and stream decoder
|   |-- store/                      CartridgeStore integration (publishing, formatting, metadata)
|   `-- utilities/                  Common utility functions for logging, environment checks, and partitions
|-- profiles/                       Default configuration files, pin profiles, and partition tables
|   |-- partitions_prg32.csv        Partition table defining resident firmware layout and eight cartridge slots
|   |-- sdkconfig.defaults*         Default configuration files for ESP32-C6 hardware and QEMU emulator builds (64 KiB cartridge window; ESP32-C6 IRAM and Wi-Fi buffer trimming for free heap; optional 128 KiB profile)
|   `-- sdkconfig.pins.*            Hardware pin configuration profiles (e.g. prototype1)
|-- tests/                          Host-side unit tests for Python tooling, PRG32 Python, the device protocol (including Wi-Fi messages), and documentation validation
|-- tools/                          Developer scripts, including real QEMU cartridge preview capture and media validation
|-- .vscode/                        Student-ready VS Code tasks and debug configurations
|-- AGENTS.md                       Rules and guidelines for autonomous coding agents
|-- CMakeLists.txt                  Top-level CMake project definition
|-- CONTRIBUTING.md                 Guidelines for human contributors
|-- CONTRIBUTORS.md                 Academic contributor metadata
|-- PRG32.code-workspace            VS Code workspace configuration
`-- pyproject.toml                  Python package configuration for the PRG32 tooling
```
