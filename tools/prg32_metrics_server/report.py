from __future__ import annotations

import sqlite3
from contextlib import closing
from pathlib import Path
from statistics import mean, median


APP_DIR = Path(__file__).resolve().parent


def connect(db_path: str | Path) -> sqlite3.Connection:
    db = sqlite3.connect(Path(db_path))
    db.row_factory = sqlite3.Row
    return db


def _quantile(values: list[int], q: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return float(values[0])
    ordered = sorted(values)
    pos = (len(ordered) - 1) * q
    low = int(pos)
    high = min(low + 1, len(ordered) - 1)
    frac = pos - low
    return float(ordered[low] * (1.0 - frac) + ordered[high] * frac)


def load_run(db_path: str | Path, run_id: str) -> dict | None:
    with closing(connect(db_path)) as db:
        row = db.execute("SELECT * FROM runs WHERE run_id = ?", (run_id,)).fetchone()
        return dict(row) if row else None


def load_samples(db_path: str | Path, run_id: str) -> list[dict]:
    with closing(connect(db_path)) as db:
        rows = db.execute(
            """
            SELECT frame, timestamp_ms, update_us, draw_us, present_us, frame_us,
                   heap_free, heap_min_free, input_mask, fps_x100,
                   upload_queue_depth, deadline_missed
            FROM samples
            WHERE run_id = ?
            ORDER BY frame ASC
            """,
            (run_id,),
        ).fetchall()
    return [dict(row) for row in rows]


def summarize_samples(samples: list[dict]) -> dict:
    frame_times = [int(sample["frame_us"]) for sample in samples]
    update_times = [int(sample["update_us"]) for sample in samples]
    draw_times = [int(sample["draw_us"]) for sample in samples]
    present_times = [int(sample["present_us"]) for sample in samples]
    missed = sum(1 for sample in samples if int(sample["deadline_missed"]))
    fps_values = [int(sample["fps_x100"]) / 100.0 for sample in samples]

    if not frame_times:
        return {
            "sample_count": 0,
            "frame_us_avg": 0.0,
            "frame_us_median": 0.0,
            "frame_us_p95": 0.0,
            "frame_us_max": 0,
            "update_us_avg": 0.0,
            "draw_us_avg": 0.0,
            "present_us_avg": 0.0,
            "fps_avg": 0.0,
            "deadline_missed": 0,
        }

    return {
        "sample_count": len(samples),
        "frame_us_avg": mean(frame_times),
        "frame_us_median": median(frame_times),
        "frame_us_p95": _quantile(frame_times, 0.95),
        "frame_us_max": max(frame_times),
        "update_us_avg": mean(update_times),
        "draw_us_avg": mean(draw_times),
        "present_us_avg": mean(present_times),
        "fps_avg": mean(fps_values) if fps_values else 0.0,
        "deadline_missed": missed,
    }


def generate_markdown_report(db_path: str | Path, run_id: str) -> str:
    run = load_run(db_path, run_id)
    if not run:
        return f"# PRG32 Metrics Report\n\nRun `{run_id}` was not found.\n"

    samples = load_samples(db_path, run_id)
    summary = summarize_samples(samples)
    dropped = int(run.get("dropped_samples") or 0)
    deadline_pct = 0.0
    if summary["sample_count"]:
        deadline_pct = 100.0 * summary["deadline_missed"] / summary["sample_count"]

    lines = [
        "# PRG32 Metrics Report",
        "",
        f"- Run: `{run_id}`",
        f"- Board: `{run['board_id']}`",
        f"- Target: `{run['target']}`",
        f"- Display: `{run['display_backend']}`",
        f"- Firmware: `{run['firmware_version']}` (`{run['firmware_git_sha']}`)",
        f"- Game: `{run['game_name']}`",
        f"- Sample period: {run['sample_period_frames']} frame(s)",
        f"- Dropped samples: {dropped}",
        "",
        "| Metric | Value |",
        "|---|---:|",
        f"| Samples | {summary['sample_count']} |",
        f"| Average frame work | {summary['frame_us_avg']:.1f} us |",
        f"| Median frame work | {summary['frame_us_median']:.1f} us |",
        f"| p95 frame work | {summary['frame_us_p95']:.1f} us |",
        f"| Max frame work | {summary['frame_us_max']} us |",
        f"| Average update | {summary['update_us_avg']:.1f} us |",
        f"| Average draw | {summary['draw_us_avg']:.1f} us |",
        f"| Average present | {summary['present_us_avg']:.1f} us |",
        f"| Average active FPS | {summary['fps_avg']:.2f} |",
        f"| Deadline misses | {summary['deadline_missed']} ({deadline_pct:.1f}%) |",
        "",
    ]
    return "\n".join(lines)


def list_runs(db_path: str | Path) -> list[dict]:
    with closing(connect(db_path)) as db:
        rows = db.execute(
            """
            SELECT runs.*,
                   COUNT(samples.id) AS sample_count
            FROM runs
            LEFT JOIN samples ON samples.run_id = runs.run_id
            GROUP BY runs.run_id
            ORDER BY runs.created_at DESC
            """
        ).fetchall()
    return [dict(row) for row in rows]
