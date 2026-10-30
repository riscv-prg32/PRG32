# PRG32 Memory Architecture & Analysis

The ESP32-C6 is a highly capable SoC, but memory management is critical for performance and stability, especially in gaming environments like PRG32. This document explains the hardware memory layout and the tools available to analyze it.

The ILI9341 backend reserves 64,000 bytes for the 320x200 indexed game
framebuffer, 512 bytes for its 256-entry RGB565 palette, and 5,120 bytes for the
retained 8-row RGB565 SPI strip. This is 63,488 bytes less than the former
128,000-byte RGB565 game framebuffer plus the same strip buffer. Boot logs
record free heap, largest free block, and minimum-ever free heap after display
initialization, before cartridge loading, and immediately before cartridge
initialization.

The Store browser uses an 8,192-byte sliding JSON input window instead of
retaining a complete catalog body. Its result entries and HTTP client still
consume heap; use the boot and runtime checkpoints below to measure their
actual impact on a board. The default audio configuration has eight voices at
22,050 Hz.

## Maximizing Free Heap

The resident runtime keeps as much internal SRAM free for the heap as it can
next to the static 64 KiB cartridge window, `prg32_cart_exec`
([profiles](../usage/profiles.md)). Each measure below was checked on the
ESP32-C6 prototype and in QEMU.

1. **No heap copy when a cartridge starts.** `prg32_cart_load_stored()` maps
   the stored image with `esp_partition_mmap()`, validates it in place, and
   copies the code/data payload once, from flash into `prg32_cart_exec`.
   Store artwork and metadata stay in flash and are never mapped. The mapping
   stays open while the cartridge is loaded because its AUDIO samples and
   tracker events play directly from flash; it is released when another slot
   loads or when that slot is erased or rewritten.
2. **Streamed uploads.** USB uploads, Store downloads, and `POST /api/games`
   write the image to flash in chunks of at most 4,096 bytes instead of
   buffering the whole package. `prg32_cart_stream_end()` then checks the
   header, sizes, and payload CRC on the flash mapping.
3. **Less code in IRAM (ESP32-C6).** Code placed in IRAM uses the same SRAM
   as the heap. `profiles/sdkconfig.defaults.esp32c6` runs these from flash:
   the Wi-Fi driver's throughput paths (`ESP_WIFI_IRAM_OPT`,
   `ESP_WIFI_EXTRA_IRAM_OPT`, `ESP_WIFI_RX_IRAM_OPT`, `ESP_WIFI_SLP_IRAM_OPT`),
   task-context ring-buffer functions, and the SPI master interrupt handler and
   HAL, which the ILI9341 driver never needs because it uses polling
   transactions only. ISR-safe ring-buffer functions and the flash driver stay
   in IRAM. Classroom uploads and score traffic do not need peak Wi-Fi
   throughput. FreeRTOS stays in IRAM on purpose:
   `FREERTOS_PLACE_FUNCTIONS_INTO_FLASH` would free about 10 KiB but enlarges
   the newlib lock code placed before `prg32_cart_exec` and moves the window
   from 0x408023a0, the address legacy cartridges are linked against (see
   [cartridge ABI compatibility](../agents/cartridge_abi_compatibility.md)).
4. **Fewer Wi-Fi receive buffers.** `ESP_WIFI_STATIC_RX_BUFFER_NUM` is 6
   instead of 10, saving 6.8 KiB.
5. **Smaller device-protocol task.** The USB protocol task stack is 4,608
   bytes instead of 8,192; its measured worst case, including streaming a
   5 KB performance result, is 2,712 bytes. Its transmit ring is 1,024 bytes
   instead of 4,096 and replies are written in pieces. The receive ring stays
   at 4,096 bytes because the USB Serial/JTAG driver drops bytes that do not
   fit while a 4,124-byte upload frame arrives.
6. **QEMU frame in panel memory.** The QEMU RGB backend draws directly into
   the virtual panel's own framebuffer, which is outside the emulated SRAM,
   instead of a 153,600-byte RGB565 copy in `.bss`. The window still updates
   only when `prg32_gfx_present()` sends the dirty rows to the panel.

The `httpd` (8,192 bytes), mDNS, lwIP, and esp_timer stacks are unchanged
because the HTTP and Wi-Fi paths could not be exercised under load when these
measurements were taken.

### Measured Effect

Free heap is reported by the device-protocol HELLO reply
([`runtime.heap_free`](../software/usb_protocol.md#hello-response)). The
figures below were taken on 2026-10-29 with the default 64 KiB window, the
setup menu open, and the same Wi-Fi state; static sizes are from `idf.py size`.

| Build | Static D/IRAM | Free heap | Largest free block |
|---|---:|---:|---:|
| ESP32-C6 `prototype1`, before | 307,385 B | 133,040 B | 106,496 B |
| ESP32-C6 `prototype1`, after | 252,926 B | 187,904 B | 159,744 B |
| ESP32-C6 `prototype1`, with stored Wi-Fi networks (2026-10-30) | - | 185,832 B | 159,744 B |
| QEMU (ESP32-C3), before | 296,744 B | not reported | not reported |
| QEMU (ESP32-C3), after | 143,152 B | 213,648 B | 114,688 B |

On the board, loading the same cartridges again and again left free heap
unchanged (within 0.2 KiB). Some cartridges start firmware services on first
use, which lowers free heap once; running all 32 Store cartridges in a row
lowered it by about 22 KiB in total
([Store compatibility check](../cartridge_store/cartridge_store.md#store-compatibility-check-2026-10-29)). With the optional 128 KiB profile, a cartridge with a 96 KiB
initialized table and a 24 KiB `.bss` buffer (`mem_size` 125,832 bytes) loaded
from flash and ran on both targets.

To repeat the per-task analysis, build with `CONFIG_HEAP_TASK_TRACKING=y` and
`CONFIG_FREERTOS_USE_TRACE_FACILITY=y`, then read the boot checkpoints,
per-task heap totals (`heap_caps_get_per_task_info()`), and stack high-water
marks (`uxTaskGetSystemState()`) after exercising uploads, audio, and
performance retrieval.

## ESP32-C6 Memory Layout

The memory of the ESP32-C6 can be broadly divided into three main components: **Flash**, **ROM**, and **RAM**.

### 1. Flash (External Memory)
The ESP32-C6 module relies on an external SPI Flash chip to store your compiled firmware, assets, and the partition table. 
- **Mapping:** The hardware Memory Management Unit (MMU) maps this external flash directly into the CPU's address space via the Cache. 
- **Address Range:** `0x4200_0000` to `0x43FF_FFFF`.
- **Usage:** Typically holds `.text` (executable code that is executed directly from flash, sometimes referred to as IROM) and `.rodata` (read-only data/constants like `prg32_splash_logo`, sometimes referred to as DROM).

### 2. ROM (Internal Boot ROM)
The chip contains a hardcoded internal ROM that holds the first-stage bootloader and core system functions (like basic UART and SPI routines). This is burned into the silicon at the factory and cannot be changed.

### 3. RAM (Internal Memory)
RAM is the fast, internal silicon memory used for variables, the heap, and high-performance code. The ESP32-C6 RAM is divided into specialized sections:

#### HP SRAM (High-Performance SRAM)
The main internal RAM consists of 512 KB of High-Performance SRAM. 
- **Address Range:** `0x4080_0000` to `0x4087_FFFF`.
- **Usage:** This is where your `.data` (initialized variables), `.bss` (zero-initialized variables), and the dynamic heap live. 

**IRAM vs. DRAM:**
The HP SRAM is a unified block of memory, but it is accessed via different hardware buses depending on what the CPU is doing:
- **IRAM (Instruction RAM):** When the CPU fetches executable code from SRAM, it uses the instruction bus. Code placed here (using the `IRAM_ATTR` macro) executes significantly faster than code in Flash and is essential for interrupt handlers (ISRs). The executable cartridge buffer `prg32_cart_exec` is explicitly placed in the `.iram1.data` section with 16-byte alignment (`__attribute__((aligned(16)))`) so uploaded games run from this high-speed memory.
- **DRAM (Data RAM):** When the CPU reads or writes variables, it uses the data bus. The exact same physical SRAM is treated as DRAM when accessed this way. 

#### LP SRAM (Low-Power SRAM)
A smaller 16 KB block of memory that remains powered on even when the main CPU goes into deep sleep. 
- **Address Range:** `0x5000_0000` to `0x5000_3FFF`.
- **Usage:** Used for the RTC (Real-Time Clock) controller, wake stubs, and variables that must survive deep sleep (`RTC_DATA_ATTR`).

---

## Memory Analysis Tooling

To help you monitor and optimize PRG32 firmware and games, PRG32 ships a memory analysis tool.

### Running the Tool
You can analyze both the static firmware size and the live dynamic heap usage with a single command:
```bash
python3 -m prg32 esp32c6 memory --url http://192.168.4.1
```

### 1. Static Memory Analysis (Build-time)
When you run the tool, it first inspects the `build-esp32c6/PRG32.map` file generated during your last build. It provides a breakdown of how much Flash and RAM your firmware consumes statically (before it even boots).

**Advanced Static Options:**
- **Component Breakdown (`--details`):** To see exactly which `.o` files are taking up space inside a specific component, use the `--details` flag.
  ```bash
  # See per-file sizes for the PRG32 library (libprg32.a)
  python3 -m prg32 esp32c6 memory --details prg32
  
  # See per-file sizes for the entire project
  python3 -m prg32 esp32c6 memory --details all
  ```
- **Microscopic Symbol View (`--top-symbols`):** To see the absolute largest variables and functions across the entire firmware, use the `--top-symbols N` flag. This uses the RISC-V `nm` tool to parse the ELF file.
  ```bash
  python3 -m prg32 esp32c6 memory --top-symbols 15
  ```
- **Hardware-Aware Filtering (`--symbol-filter`):** When looking at top symbols, you can filter them by memory type. For example, filtering by `ram` will strictly check the hardware addresses to ensure you only see objects taking up precious internal SRAM (including IRAM functions!), completely ignoring huge `.rodata` assets sitting safely in external Flash.
  ```bash
  python3 -m prg32 esp32c6 memory --top-symbols 10 --symbol-filter ram
  ```

### 2. Dynamic Memory Analysis (Run-time)
After the static analysis, the tool makes an HTTP request to the running ESP32-C6 (via the PRG32 web server API) to query the exact state of the dynamic heap in real-time.

**Task Tracking:**
By default, the tool shows total free and allocated heap space. If you want to see exactly which FreeRTOS tasks (e.g., `main`, `wifi`, `httpd`) are hoarding memory, you can enable Heap Task Tracking:
1. If you have not built the project before, run `python3 -m prg32 esp32c6 build`.
2. Run `python3 -m prg32 esp32c6 build-and-flash -e` (or `--enable-heap-tracking`).

When task tracking is compiled into the firmware, the memory command will automatically detect it and print a detailed table of memory consumption per task! Note that allocations made before the FreeRTOS scheduler starts (or by tasks that have been deleted) will be grouped under `System / Startup`.

**Boot Memory Checkpoints (Chronological Timeline):**
When heap tracking is enabled, the firmware also records "Boot Checkpoints." These checkpoints measure the drop in the *global* free heap between major subsystem initializations (e.g., `prg32_display_init`, `wifi_scores_init`).

It is important to understand the difference between these two tables:
- **Task Tracking** shows memory **Ownership**. It tracks exactly which thread's name was active when `malloc()` was called. 
- **Boot Checkpoints** shows a **Chronological Timeline**. It measures the drop in the *global* free heap between two points in time, regardless of who allocated it.

For example, if you look at a checkpoint for Wi-Fi initialization, the global free heap might drop by ~67 KiB. However, the `main` task might only own ~43 KiB total. This perfectly adds up! When `main` initializes Wi-Fi, it asks FreeRTOS to spawn new tasks. The 67 KiB global drop accounts for `main`'s allocations *plus* the OS carving out stacks for the new tasks, *plus* the background allocations made by those new tasks waking up before the initialization function even returns. The checkpoints show you *when* the memory vanished, and the Task Tracker tells you *who* took it.

**Call Stack Breakdown (Heap Tracing):**
> [!WARNING]
> Detailed heap tracing with function-level call stacks is officially disabled and unsupported in ESP-IDF v5.4 for all RISC-V architectures (including the ESP32-C6). This is due to a limitation in the GCC toolchain's `__builtin_return_address` function on RISC-V, which forces the stack depth to be clamped to 0. 
> 
> Because of this hardware/toolchain limitation, we do not provide a `--enable-heap-tracing` flag, as it cannot record the Program Counters (PCs) necessary to resolve function names. Task Tracking (`--enable-heap-tracking`) is the deepest level of memory profiling officially supported on this architecture.
