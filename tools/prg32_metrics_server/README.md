# PRG32 Metrics Server

This small Flask service receives buffered frame-performance metrics from PRG32
firmware builds that enable `CONFIG_PRG32_METRICS_ENABLE`.

The server stores one row per run and one row per sampled frame in SQLite. It is
intended for classroom labs, benchmark worksheets, and quick regression checks,
not for production telemetry.

## Install

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/prg32_metrics_server/requirements.txt
```

## Run

```bash
python3 tools/prg32_metrics_server/app.py --host 0.0.0.0 --port 8080
```

Set the firmware metrics URL to the computer running the server, for example:

```text
CONFIG_PRG32_METRICS_SERVER_URL="http://192.168.4.2:8080"
```

## Test With Sample Payloads

```bash
curl -X POST http://127.0.0.1:8080/api/runs \
  -H 'Content-Type: application/json' \
  --data @tools/prg32_metrics_server/samples/run.json

curl -X POST http://127.0.0.1:8080/api/metrics/batch \
  -H 'Content-Type: application/json' \
  --data @tools/prg32_metrics_server/samples/batch.json
```

Useful URLs:

- `http://127.0.0.1:8080/api/runs`
- `http://127.0.0.1:8080/api/runs/demo-run-001`
- `http://127.0.0.1:8080/api/runs/demo-run-001/samples.csv`
- `http://127.0.0.1:8080/api/runs/demo-run-001/report.md`

## Export A Run

```bash
python3 tools/prg32_metrics_server/export_run.py demo-run-001 \
  --db tools/prg32_metrics_server/metrics.db \
  --out metrics_export/demo-run-001
```

The export directory contains:

- `metadata.json`
- `samples.csv`
- `summary.csv`
- `table_summary.tex`
- `report.md`
- `frame_time_timeseries.png` and `frame_time_cdf.png` when matplotlib is available
