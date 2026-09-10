# PRG32 Performance Test

The canonical [Performance Test Guide](../../docs/performance_test.md) explains
QEMU and ESP32-C6 execution, HTTP retrieval, interpretation, experimental
practice, the broker ABI, and custom performance-cartridge development. This
README records only details specific to the reference cartridge package.

This normal portable cartridge is the canonical example for the public
performance broker ABI. It runs matched workloads in paired modes: the RGB565
pass uses legacy RGB565 primitives and sprites, while the full-indexed pass
uses the 8-bit clear, pixel, rectangle, and indexed-sprite APIs throughout.
Text uses the shared text API with exact fixed system-palette colors. Build
hardware and QEMU packages with:

```bash
PRG32_ARCHITECTURE=esp32c6 cartridges/performancetest/scripts/build.sh
PRG32_ARCHITECTURE=qemu cartridges/performancetest/scripts/build.sh
```

The cartridge owns all workload code, probe assets and scene state. Firmware
retains only compact aggregate results. Press B or SELECT to abort; all
temporary broker memory is released. Completed JSON remains available from
`GET /api/performance.json`.

After a successful run, the cartridge presents a per-workload results table.
LEFT and RIGHT switch between the RGB565 and indexed-color tables, and A starts
a new run. Each row displays mean FPS, mean update time in microseconds, mean
draw and present time in milliseconds, and the 95th-percentile total frame time
in milliseconds. These values correspond to `fps_mean`, `update_us_mean`,
`draw_us_mean`, `present_us_mean`, and `frame_us_p95` in each JSON
`screen_summaries` entry. The overall row omits p95 because compact schema
version 2 does not retain a combined percentile distribution.

The result page also displays the complete result-retrieval URL using the
board's current IP address. When no address is available (for example, in a
QEMU build without networking), the URL uses the explicit `<board-ip>`
placeholder instead of suggesting an unreachable hostname.
