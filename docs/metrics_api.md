# PRG32 Performance Metrics

PRG32 can collect lightweight frame-performance metrics while a cartridge is
running and upload them to a small Flask/SQLite server.

Metrics are disabled by default so ordinary classroom gameplay is unchanged.
Enable them only for profiling labs, regression checks, or trainer-led
experiments.

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
| `CONFIG_PRG32_METRICS_QUEUE_LEN` | `512` | In-RAM sample queue length |

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

Install and run the server:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/prg32_metrics_server/requirements.txt
python3 tools/prg32_metrics_server/app.py --host 0.0.0.0 --port 8080
```

Set `CONFIG_PRG32_METRICS_SERVER_URL` to the computer IP address reachable by
the ESP32-C6 or QEMU network path.

## HTTP Endpoints

| Endpoint | Purpose |
|---|---|
| `POST /api/runs` | Register or update run metadata |
| `POST /api/metrics/batch` | Store a batch of frame samples |
| `POST /api/runs/<run_id>/finish` | Mark a run as finished |
| `GET /api/runs` | List runs |
| `GET /api/runs/<run_id>` | Show one run plus summary statistics |
| `GET /api/runs/<run_id>/samples.csv` | Download raw samples |
| `GET /api/runs/<run_id>/report.md` | Download a Markdown report |

Sample payloads are in `tools/prg32_metrics_server/samples`.

## Export For Reports

```bash
python3 tools/prg32_metrics_server/export_run.py <run_id> \
  --db tools/prg32_metrics_server/metrics.db \
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
