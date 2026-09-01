# PRG32 Memory Architecture & Analysis

The ESP32-C6 is a highly capable SoC, but memory management is critical for performance and stability, especially in gaming environments like PRG32. This document explains the hardware memory layout and the tools available to analyze it.

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
- **IRAM (Instruction RAM):** When the CPU fetches executable code from SRAM, it uses the instruction bus. Code placed here (using the `IRAM_ATTR` macro) executes significantly faster than code in Flash and is essential for interrupt handlers (ISRs).
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
2. Run `python3 -m prg32 esp32c6 build-and-flash --enable-heap-tracking`.

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
