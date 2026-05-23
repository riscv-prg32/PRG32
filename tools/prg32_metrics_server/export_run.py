from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path

try:
    from .report import generate_markdown_report, load_run, load_samples, summarize_samples
except ImportError:  # pragma: no cover - direct script execution
    from report import generate_markdown_report, load_run, load_samples, summarize_samples


def write_samples_csv(path: Path, samples: list[dict]) -> None:
    fields = [
        "frame",
        "timestamp_ms",
        "update_us",
        "draw_us",
        "present_us",
        "frame_us",
        "heap_free",
        "heap_min_free",
        "input_mask",
        "fps_x100",
        "upload_queue_depth",
        "deadline_missed",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(samples)


def write_summary_csv(path: Path, summary: dict) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["metric", "value"])
        for key, value in summary.items():
            writer.writerow([key, value])


def write_summary_tex(path: Path, summary: dict) -> None:
    lines = [
        "\\begin{tabular}{lr}",
        "\\hline",
        "Metric & Value \\\\",
        "\\hline",
    ]
    for key, value in summary.items():
        label = key.replace("_", "\\_")
        if isinstance(value, float):
            lines.append(f"{label} & {value:.2f} \\\\")
        else:
            lines.append(f"{label} & {value} \\\\")
    lines.extend(["\\hline", "\\end{tabular}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_plots(output_dir: Path, samples: list[dict]) -> None:
    if os.environ.get("PRG32_METRICS_SKIP_PLOTS"):
        (output_dir / "plots_unavailable.txt").write_text(
            "Plot generation was skipped by PRG32_METRICS_SKIP_PLOTS.\n",
            encoding="utf-8",
        )
        return
    if not samples:
        (output_dir / "plots_unavailable.txt").write_text("No samples available.\n", encoding="utf-8")
        return
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as exc:  # pragma: no cover - depends on optional package
        (output_dir / "plots_unavailable.txt").write_text(
            f"matplotlib is not available: {exc}\n", encoding="utf-8"
        )
        return

    frames = [int(sample["frame"]) for sample in samples]
    frame_ms = [int(sample["frame_us"]) / 1000.0 for sample in samples]

    plt.figure(figsize=(9, 4))
    plt.plot(frames, frame_ms, linewidth=1.5)
    plt.axhline(33.333, color="red", linestyle="--", linewidth=1.0, label="30 FPS budget")
    plt.title("PRG32 frame work time")
    plt.xlabel("Frame")
    plt.ylabel("Work time (ms)")
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_dir / "frame_time_timeseries.png", dpi=140)
    plt.close()

    sorted_ms = sorted(frame_ms)
    y = [(i + 1) / len(sorted_ms) for i in range(len(sorted_ms))]
    plt.figure(figsize=(6, 4))
    plt.plot(sorted_ms, y, linewidth=1.5)
    plt.title("PRG32 frame work CDF")
    plt.xlabel("Work time (ms)")
    plt.ylabel("Cumulative probability")
    plt.tight_layout()
    plt.savefig(output_dir / "frame_time_cdf.png", dpi=140)
    plt.close()


def export_run(db_path: Path, run_id: str, output_dir: Path) -> None:
    run = load_run(db_path, run_id)
    if not run:
        raise SystemExit(f"run not found: {run_id}")
    samples = load_samples(db_path, run_id)
    summary = summarize_samples(samples)

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "metadata.json").write_text(
        json.dumps(run, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    write_samples_csv(output_dir / "samples.csv", samples)
    write_summary_csv(output_dir / "summary.csv", summary)
    write_summary_tex(output_dir / "table_summary.tex", summary)
    (output_dir / "report.md").write_text(
        generate_markdown_report(db_path, run_id), encoding="utf-8"
    )
    write_plots(output_dir, samples)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export a PRG32 metrics run.")
    parser.add_argument("run_id", help="run id to export")
    parser.add_argument("--db", default=str(Path(__file__).resolve().parent / "metrics.db"))
    parser.add_argument("--out", default="metrics_export")
    args = parser.parse_args()

    export_run(Path(args.db), args.run_id, Path(args.out))


if __name__ == "__main__":
    main()
