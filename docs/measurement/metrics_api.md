# PRG32 Performance Metrics

PRG32 can collect lightweight frame-performance metrics in two ways:

- the setup-mode **Performance Test** runs an unattended benchmark and stores
  raw samples plus aggregate windows in RAM on the board or QEMU instance
- the optional streaming metrics pipeline records cartridge frames and uploads
  buffered batches to a small Flask/SQLite server

The setup performance test is available in normal firmware builds. Streaming
metrics are disabled by default so ordinary classroom gameplay is unchanged.
Enable streaming only for profiling labs, regression checks, or trainer-led
experiments.

## Setup Performance Test

Open setup mode and choose `PERFORMANCE TEST`. The firmware starts the Wi-Fi
HTTP API if possible, then runs every measurement screen without further user
interaction. At the end it shows a summary on the 320x240 setup screen.

The unattended sequence measures five distinct screens in both `rgb565` and
`indexed` color modes. Each frame includes the same 24-sprite color probe; the
RGB565 pass reads 16-bit pixels, while the indexed pass reads packed 2-bpp
indices and a shared RGB565 palette. Scene state is reset between passes so the
two measurements are directly comparable.

| Screen | What It Measures |
|---|---|
| `clear-fill` | viewport clear and large rectangle fill bandwidth |
| `text-overlay` | 8x8 text drawing and status overlay load |
| `sprite-storm` | many moving sprite-sized rectangles and collision-like motion |
| `scrolling` | horizontal and vertical scrolling with parallax-like stars |
| `mixed-gameplay` | combined text, sprites, scrolling road, and playfield objects |

The test stores results only in RAM:

- a new run replaces the previous run
- rebooting the board or QEMU clears the results
- no per-frame network traffic is generated during the benchmark

Download the JSON file after the summary is visible:

```bash
curl http://192.168.4.1/api/performance.json \
  --output prg32_performance.json
```

Use the current setup IP address when the board is in infrastructure mode.
The endpoint streams the response in HTTP chunks, so the ESP32 does not need to
allocate a second full copy of the raw sample set while serving the file.

The JSON contains top-level run metadata, raw sampled frames, aggregate windows,
and a summary object. The run metadata includes:

| Field | Meaning |
|---|---|
| `run_id` | unique in-RAM run identifier |
| `board_id` | board label from metrics configuration defaults |
| `target` | `esp32c6` or `qemu-esp32c3` |
| `display_backend` | `ili9341` or `qemu_rgb` |
| `firmware_git_sha` | firmware source identifier when available |
| `firmware_version` | ESP-IDF application version string |
| `game_name` | `setup-performance-test` for this built-in benchmark |
| `cartridge_generation` | loaded cartridge generation counter |
| `build_type` | `release` or `debug` |
| `wifi_mode` | `off`, `access_point`, `infrastructure`, or `ap_infrastructure` |
| `sample_period_frames` | frame sampling period |
| `screen_count` | number of measurement screens in the unattended run |
| `result_count` | screen/mode results; currently 10 (5 screens × 2 modes) |
| `color_modes` | ordered list containing `rgb565` and `indexed` |
| `started_at_device_us` | ESP timer timestamp when the run began |
| `started_at_server_ts` | `null` for onboard-only runs |

Each raw sample records:

| Field | Meaning |
|---|---|
| `frame_index` | benchmark frame number |
| `screen_index` | zero-based measurement screen index |
| `screen_name` | measurement screen name |
| `color_mode` | asset rendering path: `rgb565` or `indexed` |
| `t_update_us` | update stage time |
| `t_draw_us` | draw stage time |
| `t_present_us` | display present time |
| `t_frame_total_us` | update + draw + present time |
| `deadline_missed` | true when the frame exceeds 33333 us |
| `free_heap_bytes` | free heap at sample time |
| `min_free_heap_bytes` | ESP-IDF minimum free heap |
| `input_mask` | merged menu input mask |
| `upload_queue_depth` | streaming queue depth, zero for onboard tests |

Each aggregate window includes `frames`, `fps_mean`, frame-time min/mean/p50/p95/p99/max,
missed deadlines, update/draw/present means, and minimum heap.

The `screen_summaries` array contains one aggregate per screen and color mode.
The `comparisons` array groups the RGB565 and indexed aggregates for each
screen under one object. The on-device summary likewise displays a single table
with `RGB FPS` and `IDX FPS` columns.

### QEMU reference result

The following matrix was captured from an actual ESP32-C3 QEMU run of firmware
revision `58dde55-dirty` on 2026-09-04. It is a functional reference showing
that both paths execute in one paired run; QEMU timings must not be presented as
ESP32-C6 hardware measurements.

This capture predates the direct-row sprite optimization and remains only a
workflow/baseline artifact. Do not use its values to characterize the current
renderer. Publish updated numbers only after repeating the paired run on the
target revision; ESP32-C6 claims require measurements on real ESP32-C6 hardware.

![QEMU RGB565 and indexed-color performance matrix](images/performance_rgb565_vs_indexed.png)

| Workload | RGB565 FPS | Indexed FPS | Indexed difference |
|---|---:|---:|---:|
| clear-fill | 33.29 | 33.46 | +0.17 |
| text-overlay | 33.08 | 33.28 | +0.20 |
| sprite-storm | 33.48 | 33.14 | -0.34 |
| scrolling | 33.55 | 33.42 | -0.13 |
| mixed-gameplay | 33.43 | 33.07 | -0.36 |

The run reported 33.32 overall FPS, 30,008 us mean frame work, 31,372 us p95,
zero missed deadlines across 600 frames, and 34,920 bytes minimum free heap.
Because both modes share the same RGB565 framebuffer and present stage, small
differences primarily reflect compact-pixel decoding plus normal QEMU timing
variation. Repeat paired runs and use the JSON samples for statistical claims.

Compact example:

```json
{
  "screen_count": 5,
  "result_count": 10,
  "color_modes": ["rgb565", "indexed"],
  "samples": [
    {"screen_index": 0, "screen_name": "clear-fill", "color_mode": "rgb565"}
  ],
  "screen_summaries": [
    {"screen_index": 0, "screen_name": "clear-fill", "color_mode": "rgb565", "fps_mean": 30.1},
    {"screen_index": 0, "screen_name": "clear-fill", "color_mode": "indexed", "fps_mean": 29.8}
  ],
  "comparisons": [
    {
      "screen_index": 0,
      "screen_name": "clear-fill",
      "rgb565": {"fps_mean": 30.1, "frame_us_mean": 33220, "frame_us_p95": 33410},
      "indexed": {"fps_mean": 29.8, "frame_us_mean": 33540, "frame_us_p95": 33800}
    }
  ]
}
```

`screen_count` remains the number of distinct workloads. `result_count` is the
number of workload/mode combinations. This lets older consumers keep treating
the benchmark as five screens while mode-aware consumers process ten results.

## Firmware Configuration

Open `idf.py menuconfig`, then enable:

```text
Component config -> PRG32 framework -> Performance metrics
```

For repeatable lab builds, use the checked-in metrics defaults profile together
with the normal board defaults:

```bash
idf.py -B build-esp32c6-metrics \
  -D SDKCONFIG=build-esp32c6-metrics/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.metrics" \
  set-target esp32c6
idf.py -B build-esp32c6-metrics \
  -D SDKCONFIG=build-esp32c6-metrics/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.metrics" \
  build
```

Useful options:

| Option | Default | Purpose |
|---|---:|---|
| `CONFIG_PRG32_METRICS_ENABLE` | off | Enables buffered metrics collection |
| `CONFIG_PRG32_METRICS_SERVER_URL` | `http://192.168.4.2:8080` | Metrics server base URL |
| `CONFIG_PRG32_METRICS_BOARD_ID` | `prg32-board` | Board name stored with each run |
| `CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES` | `1` | Record one sample every N frames |
| `CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS` | `5000` | Background upload period |
| `CONFIG_PRG32_METRICS_QUEUE_LEN` | `128` | Sample queue length allocated when a metrics run starts |

The runtime starts a metrics run when a cartridge starts and stops the run when
the cartridge is replaced or unloaded.

## Firmware API

Public declarations live in `components/prg32/include/prg32_metrics.h`.

```c
int prg32_metrics_init(const prg32_metrics_config_t *config);
int prg32_metrics_start_run(void);
int prg32_metrics_stop_run(void);
int prg32_metrics_is_enabled(void);
int prg32_metrics_record(const prg32_metric_sample_t *sample);
const char *prg32_metrics_run_id(void);
```

`prg32_metrics_record` is intentionally small: it copies a sample into a ring
buffer and returns. Network upload happens in a background task. If the queue is
full, PRG32 drops the new sample and reports the dropped-sample count with a
later batch.

## Sample Fields

Each sampled frame records:

| Field | Meaning |
|---|---|
| `frame` | PRG32 frame counter |
| `timestamp_ms` | firmware uptime in milliseconds |
| `update_us` | cartridge update time |
| `draw_us` | cartridge draw time |
| `present_us` | display present time |
| `frame_us` | update + draw + present work time |
| `heap_free` | current free heap |
| `heap_min_free` | minimum free heap seen by ESP-IDF |
| `input_mask` | last PRG32 input bitmask |
| `fps_x100` | active-work FPS multiplied by 100 |
| `upload_queue_depth` | queue depth after enqueue |
| `deadline_missed` | true when frame work exceeds the 30 FPS budget |

## Server Setup

Install and run the standalone
[MetricsServer](https://github.com/riscv-prg32/MetricsServer):

```bash
git clone https://github.com/riscv-prg32/MetricsServer.git
cd MetricsServer
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.txt
python3 app.py --host 0.0.0.0 --port 8080
```

Set `CONFIG_PRG32_METRICS_SERVER_URL` to the computer IP address reachable by
the ESP32-C6 or QEMU network path.

## HTTP Endpoints

| Endpoint | Purpose |
|---|---|
| `GET /api/performance.json` | Download the latest setup performance test JSON |
| `POST /api/runs` | Register or update run metadata |
| `POST /api/metrics/batch` | Store a batch of frame samples |
| `POST /api/runs/<run_id>/finish` | Mark a run as finished |
| `GET /api/runs` | List runs |
| `GET /api/runs/<run_id>` | Show one run plus summary statistics |
| `GET /api/runs/<run_id>/samples.csv` | Download raw samples |
| `GET /api/runs/<run_id>/report.md` | Download a Markdown report |

Sample payloads are in the MetricsServer repository's `samples` directory.

## Export For Reports

For setup-mode performance tests, generate paper-ready tables and figures from
the downloaded JSON:

```bash
python3 tools/prg32_metrics_paper.py prg32_performance.json \
  --out paper_metrics/prg32_run01 \
  --dpi 300
```

The output directory contains:

- `samples.csv`
- `normalized_metrics.json`
- `table_summary.tex`
- `table_windows.tex`
- `table_screens.tex`
- `captions.tex`
- `figure_frame_time_timeseries.png`
- `figure_frame_time_distribution.png`
- `figure_stage_timing.png`
- `figure_heap_stability.png`
- `figure_screen_comparison.png`

`samples.csv` includes a `color_mode` column, and `table_screens.tex` includes
a Mode column. Older JSON without `color_mode` is interpreted as RGB565 by the
report generator.

For server-side streaming metrics, export a run from SQLite inside the
MetricsServer checkout:

```bash
python3 export_run.py <run_id> \
  --db metrics.db \
  --out metrics_export/<run_id>
```

The export contains CSV data, a Markdown report, a small LaTeX table, and PNG
plots when `matplotlib` is installed.

## Classroom Exercise Idea

1. Run a cartridge with metrics disabled and observe normal FPS.
2. Enable metrics with a sample period of 1 frame.
3. Run the same cartridge for 30 seconds.
4. Export the run and compare update, draw, and present time.
5. Increase `CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES` to 5 and repeat.
6. Explain how measurement overhead and network conditions affect the results.


(?)
- For reproducible scientific measurements, use
  `sdkconfig.defaults;sdkconfig.defaults.metrics` as described in
  `docs/scientific_measurement_tutorial.md`.

## Development Guide

> [!IMPORTANT]
> This information is only intended for developers of the PRG32 framework.

### Metrics Implementation Details

- Keep `prg32_metrics_record()` non-blocking. Metrics collection must copy into
  a small queue and leave HTTP upload to the background task so profiling does
  not become the main source of frame-time jitter.
