import subprocess
import urllib.request
import urllib.error
import json
import sys

def memory_esp32c6(args):
    import os
    import shutil
    from prg32.utilities.env_variables import ESP32C6_BUILD_DIR

    print("=== Static Memory Breakdown ===")
    map_file = os.path.join(ESP32C6_BUILD_DIR, "PRG32.map")
    
    try:
        if not os.path.exists(map_file):
            print("No build found. Running idf.py size-components to build and analyze...")
            subprocess.run(["idf.py", "-B", ESP32C6_BUILD_DIR, "size-components"], check=True)
        else:
            idf_py = shutil.which("idf.py")
            if idf_py:
                idf_size = os.path.join(os.path.dirname(idf_py), "idf_size.py")
                if os.path.exists(idf_size):
                    print("Build found. Analyzing existing map file (avoiding re-build)...")
                    if args.details:
                        if args.details.lower() == "all":
                            subprocess.run([sys.executable, idf_size, "--files", map_file], check=True)
                        else:
                            subprocess.run([sys.executable, idf_size, "--files", "-F", f"*{args.details}*", map_file], check=True)
                    else:
                        subprocess.run([sys.executable, idf_size, "--archives", map_file], check=True)
                else:
                    print("Warning: idf_size.py not found. Skipping static analysis to avoid re-build.")
            else:
                print("Warning: idf.py not found in PATH.")
        if args.top_symbols:
            from prg32.utilities.env_variables import ESP32C6_ELF
            print(f"\n=== Top {args.top_symbols} Largest Symbols (via nm) ===")
            if not os.path.exists(ESP32C6_ELF):
                print(f"Warning: ELF file not found at {ESP32C6_ELF}. Cannot analyze symbols.")
            else:
                try:
                    result = subprocess.run(
                        ["riscv32-esp-elf-nm", "--print-size", "--size-sort", "--radix=d", ESP32C6_ELF],
                        capture_output=True, text=True, check=True
                    )
                    lines = result.stdout.strip().split('\n')
                    # We need to process from largest to smallest, but wait, if we filter, 
                    # we can't just take the last N lines. We must take ALL lines, reverse them,
                    # and then filter until we hit N matches.
                    lines.reverse()
                    top_lines = lines
                    
                    print(f"{'Size (Bytes)':>15} | {'Type':<4} | {'Symbol Name'}")
                    print("-" * 60)
                    
                    # IMPORTANT: The following filters rely heavily on the hardware memory map 
                    # specifically for the ESP32-C6 SoC (Technical Reference Manual).
                    # If this script is ever adapted for other chips (ESP32, ESP32-S3, etc.), 
                    # these hardcoded address boundaries MUST be updated, otherwise the filtering 
                    # will fail or produce incorrect results.
                    # It only falls back to the `nm` type character if the address doesn't match known bounds.
                    def is_ram(addr_str, typ):
                        try:
                            addr = int(addr_str, 10)
                            if 0x42000000 <= addr < 0x44000000: # External Flash
                                return False
                            if (0x40800000 <= addr < 0x40880000) or (0x50000000 <= addr < 0x50004000): # HP SRAM or LP SRAM
                                return True
                        except ValueError:
                            pass
                        return typ in "BbDdVv"
                        
                    def is_flash(addr_str, typ):
                        try:
                            addr = int(addr_str, 10)
                            if 0x42000000 <= addr < 0x44000000:
                                return True
                        except ValueError:
                            pass
                        return typ in "TtRr" and not is_ram(addr_str, typ)

                    count = 0
                    for line in top_lines:
                        if count >= args.top_symbols:
                            break
                        parts = line.split()
                        
                        addr = parts[0] if len(parts) >= 3 else "00000000"
                        
                        if len(parts) >= 4:
                            size_str = parts[1]
                            typ = parts[2]
                            name = " ".join(parts[3:])
                        elif len(parts) >= 3:
                            size_str = "0"
                            typ = parts[1]
                            name = " ".join(parts[2:])
                        else:
                            continue
                            
                        # Apply filter
                        if args.symbol_filter:
                            filt = args.symbol_filter.lower()
                            matched = False
                            if filt == "ram":
                                matched = is_ram(addr, typ)
                            elif filt in ("flash", "rom"):
                                matched = is_flash(addr, typ)
                            elif filt == "bss":
                                matched = typ in "Bb"
                            elif filt == "data":
                                matched = typ in "Dd"
                            elif filt == "code":
                                matched = typ in "Tt"
                            else:
                                # Raw character matching
                                matched = typ in args.symbol_filter
                                
                            if not matched:
                                continue
                                
                        count += 1
                        size = int(size_str) if size_str != "0" else "Unknown"
                        print(f"{size:>15} | {typ:<4} | {name}")
                        
                except FileNotFoundError:
                    print("Warning: riscv32-esp-elf-nm not found in PATH.")
                except subprocess.CalledProcessError:
                    print("Warning: Failed to run nm.")
                    
    except FileNotFoundError:
        print("Warning: ESP-IDF tools not found in PATH. Skipping static analysis.")
    except subprocess.CalledProcessError:
        print("Warning: Static analysis failed.")

    print("\n=== Dynamic Memory Breakdown (Live via HTTP API) ===")
    url = f"{args.url}/api/memory"
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=5) as response:
            data = json.loads(response.read().decode("utf-8"))
    except urllib.error.URLError as e:
        print(f"Error connecting to device at {args.url}: {e}", file=sys.stderr)
        return
    except json.JSONDecodeError:
        print(f"Error parsing JSON from device at {args.url}", file=sys.stderr)
        return

    print(f"Total Heap: {data.get('heap_total_bytes', 0) / 1024:.2f} KiB")
    print(f"Free Heap:  {data.get('heap_free_bytes', 0) / 1024:.2f} KiB")
    print(f"Allocated:  {data.get('heap_allocated_bytes', 0) / 1024:.2f} KiB")
    print(f"Largest Free Block: {data.get('heap_largest_free_block', 0) / 1024:.2f} KiB")
    
    tasks = data.get("tasks")
    if tasks is not None:
        print("\n=== Dynamic Heap Task Tracking ===")
        print(f"{'Task Name':<20} | {'Allocated (KiB)':>15}")
        print("-" * 40)
        for task in sorted(tasks, key=lambda t: t.get("allocated_bytes", 0), reverse=True):
            kb = task.get("allocated_bytes", 0) / 1024
            name = task.get('name', 'Unknown')
            if name == 'Unknown':
                name = 'System / Startup'
            print(f"{name:<20} | {kb:>15.2f}")
        checkpoints = data.get("checkpoints")
        if checkpoints:
            print("\n=== Boot Memory Checkpoints ===")
            print(f"{'Checkpoint Name':<20} | {'Consumed (KiB)':>15}")
            print("-" * 40)
            for cp in checkpoints:
                kb = cp.get("bytes", 0) / 1024
                name = cp.get("name", "Unknown")
                print(f"{name:<20} | {kb:>15.2f}")
    else:
        print("\nNOTE: Detailed dynamic task breakdown and boot checkpoints are not available.")
        print("To see exact per-task memory usage, you must build the firmware with heap tracking enabled.")
        print("Run the following command, then re-run the memory analysis:")
        print("  python3 -m prg32 esp32c6 build-and-flash --enable-heap-tracking")

    print("\nNOTE: Detailed dynamic heap tracing (Call Stack Breakdown) is officially unsupported")
    print("on ESP32-C6 and all other RISC-V architectures in ESP-IDF v5.4 due to a limitation")
    print("with the GCC toolchain's `__builtin_return_address`. Stack traces are clamped to 0.")
