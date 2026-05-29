from __future__ import annotations

import argparse
import csv
import json
import math
import os
from pathlib import Path
from statistics import mean, median
from typing import Any


FRAME_BUDGET_US = 33333


def _number(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _int(value: Any, default: int = 0) -> int:
    return int(_number(value, float(default)))


def _sample_field(sample: dict[str, Any], primary: str, fallback: str) -> int:
    if primary in sample:
        return _int(sample[primary])
    return _int(sample.get(fallback))


def _quantile(values: list[int], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return float(ordered[0])
    pos = (len(ordered) - 1) * q
    low = math.floor(pos)
    high = min(low + 1, len(ordered) - 1)
    frac = pos - low
    return float(ordered[low] * (1.0 - frac) + ordered[high] * frac)


def _latex_escape(text: Any) -> str:
    value = str(text)
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    return "".join(replacements.get(ch, ch) for ch in value)


def load_metrics(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    samples = data.get("samples", [])
    if not isinstance(samples, list):
        raise SystemExit("metrics JSON does not contain a samples array")
    return data


def normalize_samples(data: dict[str, Any]) -> list[dict[str, Any]]:
    normalized: list[dict[str, Any]] = []
    for raw in data.get("samples", []):
        frame_total = _sample_field(raw, "t_frame_total_us", "frame_us")
        normalized.append(
            {
                "frame_index": _sample_field(raw, "frame_index", "frame"),
                "screen_index": _int(raw.get("screen_index")),
                "screen_name": str(raw.get("screen_name", "unknown")),
                "t_update_us": _sample_field(raw, "t_update_us", "update_us"),
                "t_draw_us": _sample_field(raw, "t_draw_us", "draw_us"),
                "t_present_us": _sample_field(raw, "t_present_us", "present_us"),
                "t_frame_total_us": frame_total,
                "deadline_missed": bool(raw.get("deadline_missed"))
                or frame_total > FRAME_BUDGET_US,
                "free_heap_bytes": _sample_field(raw, "free_heap_bytes", "heap_free"),
                "min_free_heap_bytes": _sample_field(
                    raw, "min_free_heap_bytes", "heap_min_free"
                ),
                "input_mask": _int(raw.get("input_mask")),
                "upload_queue_depth": _int(raw.get("upload_queue_depth")),
            }
        )
    return normalized


def summarize(samples: list[dict[str, Any]]) -> dict[str, float]:
    frame = [int(sample["t_frame_total_us"]) for sample in samples]
    update = [int(sample["t_update_us"]) for sample in samples]
    draw = [int(sample["t_draw_us"]) for sample in samples]
    present = [int(sample["t_present_us"]) for sample in samples]
    heap = [int(sample["min_free_heap_bytes"]) for sample in samples]
    missed = sum(1 for sample in samples if sample["deadline_missed"])
    if not frame:
        return {
            "frames": 0,
            "fps_mean": 0.0,
            "frame_us_min": 0.0,
            "frame_us_mean": 0.0,
            "frame_us_p50": 0.0,
            "frame_us_p95": 0.0,
            "frame_us_p99": 0.0,
            "frame_us_max": 0.0,
            "missed_deadlines": 0,
            "missed_deadline_ratio": 0.0,
            "update_us_mean": 0.0,
            "draw_us_mean": 0.0,
            "present_us_mean": 0.0,
            "heap_min": 0,
            "jitter_us_std": 0.0,
        }

    avg = mean(frame)
    variance = mean([(value - avg) ** 2 for value in frame])
    return {
        "frames": len(frame),
        "fps_mean": 1_000_000.0 / avg if avg > 0 else 0.0,
        "frame_us_min": min(frame),
        "frame_us_mean": avg,
        "frame_us_p50": median(frame),
        "frame_us_p95": _quantile(frame, 0.95),
        "frame_us_p99": _quantile(frame, 0.99),
        "frame_us_max": max(frame),
        "missed_deadlines": missed,
        "missed_deadline_ratio": missed / len(frame),
        "update_us_mean": mean(update),
        "draw_us_mean": mean(draw),
        "present_us_mean": mean(present),
        "heap_min": min(heap) if heap else 0,
        "jitter_us_std": math.sqrt(variance),
    }


def write_samples_csv(path: Path, samples: list[dict[str, Any]]) -> None:
    fields = [
        "frame_index",
        "screen_index",
        "screen_name",
        "t_update_us",
        "t_draw_us",
        "t_present_us",
        "t_frame_total_us",
        "deadline_missed",
        "free_heap_bytes",
        "min_free_heap_bytes",
        "input_mask",
        "upload_queue_depth",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(samples)


def write_summary_table(path: Path, data: dict[str, Any], summary: dict[str, float]) -> None:
    rows = [
        ("Run ID", data.get("run_id", "unknown")),
        ("Board", data.get("board_id", "unknown")),
        ("Target", data.get("target", "unknown")),
        ("Display backend", data.get("display_backend", "unknown")),
        ("Firmware", data.get("firmware_version", "unknown")),
        ("Build type", data.get("build_type", "unknown")),
        ("Frames", f"{summary['frames']:.0f}"),
        ("Mean FPS", f"{summary['fps_mean']:.2f}"),
        ("Mean frame time", f"{summary['frame_us_mean']:.1f} us"),
        ("p95 frame time", f"{summary['frame_us_p95']:.1f} us"),
        ("p99 frame time", f"{summary['frame_us_p99']:.1f} us"),
        ("Frame jitter", f"{summary['jitter_us_std']:.1f} us"),
        (
            "Missed deadline ratio",
            f"{summary['missed_deadline_ratio'] * 100.0:.2f}%",
        ),
        ("Mean update", f"{summary['update_us_mean']:.1f} us"),
        ("Mean draw", f"{summary['draw_us_mean']:.1f} us"),
        ("Mean present", f"{summary['present_us_mean']:.1f} us"),
        ("Minimum heap", f"{summary['heap_min']:.0f} bytes"),
    ]
    lines = [
        r"\begin{table}[tbp]",
        r"\centering",
        r"\caption{Summary of the unattended PRG32 performance test. "
        r"The table reports mean FPS, frame-time distribution, stage timing, "
        r"deadline misses, and memory stability for the selected board and display backend.}",
        r"\label{tab:prg32-performance-summary}",
        r"\begin{tabular}{lr}",
        r"\hline",
        r"Metric & Value \\",
        r"\hline",
    ]
    for label, value in rows:
        lines.append(f"{_latex_escape(label)} & {_latex_escape(value)} \\\\")
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_window_table(path: Path, data: dict[str, Any]) -> None:
    windows = data.get("aggregate_windows", [])
    lines = [
        r"\begin{table}[tbp]",
        r"\centering",
        r"\caption{Per-window PRG32 frame-time aggregates. "
        r"Each window summarizes a contiguous interval of the unattended run, "
        r"highlighting temporal stability and deadline behavior.}",
        r"\label{tab:prg32-performance-windows}",
        r"\begin{tabular}{rrrrrrr}",
        r"\hline",
        r"Window & Frames & FPS & Mean us & p95 us & p99 us & Missed \\",
        r"\hline",
    ]
    for window in windows:
        lines.append(
            f"{_int(window.get('window_index'))} & "
            f"{_int(window.get('frames'))} & "
            f"{_number(window.get('fps_mean')):.2f} & "
            f"{_number(window.get('frame_us_mean')):.1f} & "
            f"{_number(window.get('frame_us_p95')):.1f} & "
            f"{_number(window.get('frame_us_p99')):.1f} & "
            f"{_int(window.get('missed_deadlines'))} \\\\"
        )
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_screen_table(path: Path, data: dict[str, Any]) -> None:
    screens = data.get("screen_summaries", [])
    lines = [
        r"\begin{table}[tbp]",
        r"\centering",
        r"\caption{Per-screen PRG32 benchmark results. "
        r"Each row reports one unattended measurement screen with a distinct "
        r"graphics workload, enabling separate analysis of clear/fill, text, "
        r"sprite, scrolling, and mixed-game rendering costs.}",
        r"\label{tab:prg32-performance-screens}",
        r"\begin{tabular}{lrrrrr}",
        r"\hline",
        r"Screen & FPS & Mean us & p95 us & p99 us & Missed \\",
        r"\hline",
    ]
    for screen in screens:
        lines.append(
            f"{_latex_escape(screen.get('screen_name', 'unknown'))} & "
            f"{_number(screen.get('fps_mean')):.2f} & "
            f"{_number(screen.get('frame_us_mean')):.1f} & "
            f"{_number(screen.get('frame_us_p95')):.1f} & "
            f"{_number(screen.get('frame_us_p99')):.1f} & "
            f"{_int(screen.get('missed_deadlines'))} \\\\"
        )
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_captions(path: Path, data: dict[str, Any]) -> None:
    run = data.get("run_id", "unknown")
    target = data.get("target", "unknown")
    display = data.get("display_backend", "unknown")
    lines = [
        "% PRG32 generated captions",
        f"% Run: {_latex_escape(run)}",
        "",
        "\\newcommand{\\PRGThirtyTwoPerfSummaryCaption}{Summary of the PRG32 "
        f"unattended performance run on {_latex_escape(target)} using the "
        f"{_latex_escape(display)} display backend.}}",
        r"\newcommand{\PRGThirtyTwoFrameTimelineCaption}{Frame-time time series "
        r"for the PRG32 unattended performance test. The dashed reference line "
        r"marks the 33.33 ms frame budget required for 30 FPS.}",
        r"\newcommand{\PRGThirtyTwoFrameDistributionCaption}{Distribution of "
        r"frame times for the PRG32 unattended performance test, highlighting "
        r"median, p95, and p99 behavior for reproducibility analysis.}",
        r"\newcommand{\PRGThirtyTwoStageTimingCaption}{Mean update, draw, and "
        r"present timing per sampled frame, showing where the runtime spends "
        r"time during the benchmark workload.}",
        r"\newcommand{\PRGThirtyTwoHeapCaption}{Heap stability during the PRG32 "
        r"performance test. A flat minimum-free-heap trace supports the absence "
        r"of per-frame allocation drift.}",
        r"\newcommand{\PRGThirtyTwoScreenComparisonCaption}{Per-screen PRG32 "
        r"benchmark comparison. The grouped workloads isolate display clear, "
        r"text overlay, sprite, scrolling, and mixed-game rendering costs.}",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def write_plots(output_dir: Path,
                samples: list[dict[str, Any]],
                data: dict[str, Any],
                dpi: int) -> None:
    if os.environ.get("PRG32_METRICS_SKIP_PLOTS"):
        (output_dir / "plots_unavailable.txt").write_text(
            "Plot generation skipped by PRG32_METRICS_SKIP_PLOTS.\n",
            encoding="utf-8",
        )
        return
    if not samples:
        (output_dir / "plots_unavailable.txt").write_text(
            "No samples available for plotting.\n", encoding="utf-8"
        )
        return
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as exc:  # pragma: no cover - optional plotting dependency
        (output_dir / "plots_unavailable.txt").write_text(
            f"matplotlib is not available: {exc}\n",
            encoding="utf-8",
        )
        return

    try:
        plt.style.use("seaborn-v0_8-whitegrid")
    except Exception:
        pass

    frames = [int(sample["frame_index"]) for sample in samples]
    frame_ms = [int(sample["t_frame_total_us"]) / 1000.0 for sample in samples]
    update_ms = [int(sample["t_update_us"]) / 1000.0 for sample in samples]
    draw_ms = [int(sample["t_draw_us"]) / 1000.0 for sample in samples]
    present_ms = [int(sample["t_present_us"]) / 1000.0 for sample in samples]
    heap_kib = [int(sample["min_free_heap_bytes"]) / 1024.0 for sample in samples]
    screen_markers: list[tuple[int, str]] = []
    seen_screens: set[int] = set()
    for sample in samples:
        screen_index = int(sample.get("screen_index", 0))
        if screen_index in seen_screens:
            continue
        seen_screens.add(screen_index)
        screen_markers.append((int(sample["frame_index"]), str(sample.get("screen_name", ""))))

    accent = "#05a6d8"
    warning = "#e8327c"
    yellow = "#f2c94c"
    dark = "#20242a"

    fig, ax = plt.subplots(figsize=(8.8, 4.6))
    ax.plot(frames, frame_ms, color=accent, linewidth=1.7)
    ax.axhline(33.333, color=warning, linestyle="--", linewidth=1.1, label="30 FPS budget")
    for marker, name in screen_markers[1:]:
        ax.axvline(marker, color="#8d99ae", linestyle=":", linewidth=0.8)
        ax.text(marker, 0.98, name, rotation=90, va="top", ha="right",
                transform=ax.get_xaxis_transform(), fontsize=7)
    ax.set_title("PRG32 frame-time time series")
    ax.set_xlabel("Frame index")
    ax.set_ylabel("Frame time (ms)")
    ax.legend(frameon=True)
    fig.tight_layout()
    fig.savefig(output_dir / "figure_frame_time_timeseries.png", dpi=dpi)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    ax.hist(frame_ms, bins=min(30, max(8, len(frame_ms) // 10)), color=accent, alpha=0.82)
    ax.axvline(_quantile([int(v * 1000) for v in frame_ms], 0.50) / 1000.0,
               color=dark, linewidth=1.2, label="p50")
    ax.axvline(_quantile([int(v * 1000) for v in frame_ms], 0.95) / 1000.0,
               color=yellow, linewidth=1.2, label="p95")
    ax.axvline(_quantile([int(v * 1000) for v in frame_ms], 0.99) / 1000.0,
               color=warning, linewidth=1.2, label="p99")
    ax.set_title("PRG32 frame-time distribution")
    ax.set_xlabel("Frame time (ms)")
    ax.set_ylabel("Samples")
    ax.legend(frameon=True)
    fig.tight_layout()
    fig.savefig(output_dir / "figure_frame_time_distribution.png", dpi=dpi)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8.8, 4.6))
    ax.stackplot(
        frames,
        update_ms,
        draw_ms,
        present_ms,
        labels=["update", "draw", "present"],
        colors=["#25c2a0", "#05a6d8", "#e8327c"],
        alpha=0.9,
    )
    ax.set_title("PRG32 frame-stage timing")
    ax.set_xlabel("Frame index")
    ax.set_ylabel("Stage time (ms)")
    ax.legend(loc="upper right", frameon=True)
    fig.tight_layout()
    fig.savefig(output_dir / "figure_stage_timing.png", dpi=dpi)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8.8, 4.2))
    ax.plot(frames, heap_kib, color="#6c5ce7", linewidth=1.7)
    ax.set_title("PRG32 minimum free heap")
    ax.set_xlabel("Frame index")
    ax.set_ylabel("Minimum free heap (KiB)")
    fig.tight_layout()
    fig.savefig(output_dir / "figure_heap_stability.png", dpi=dpi)
    plt.close(fig)

    screens = data.get("screen_summaries", [])
    if screens:
        names = [str(screen.get("screen_name", "unknown")) for screen in screens]
        fps_values = [_number(screen.get("fps_mean")) for screen in screens]
        p95_values = [_number(screen.get("frame_us_p95")) / 1000.0 for screen in screens]

        fig, ax1 = plt.subplots(figsize=(8.8, 4.8))
        x = list(range(len(names)))
        ax1.bar(x, fps_values, color=accent, alpha=0.86, label="Mean FPS")
        ax1.set_ylabel("Mean FPS")
        ax1.set_xticks(x)
        ax1.set_xticklabels(names, rotation=18, ha="right")
        ax2 = ax1.twinx()
        ax2.plot(x, p95_values, color=warning, marker="o", linewidth=1.8, label="p95 frame")
        ax2.set_ylabel("p95 frame time (ms)")
        ax1.set_title("PRG32 per-screen benchmark comparison")
        lines1, labels1 = ax1.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right", frameon=True)
        fig.tight_layout()
        fig.savefig(output_dir / "figure_screen_comparison.png", dpi=dpi)
        plt.close(fig)


def process_metrics(input_path: Path, output_dir: Path, dpi: int) -> None:
    data = load_metrics(input_path)
    samples = normalize_samples(data)
    summary = summarize(samples)

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "normalized_metrics.json").write_text(
        json.dumps({"metadata": data, "summary": summary, "samples": samples}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    write_samples_csv(output_dir / "samples.csv", samples)
    write_summary_table(output_dir / "table_summary.tex", data, summary)
    write_window_table(output_dir / "table_windows.tex", data)
    write_screen_table(output_dir / "table_screens.tex", data)
    write_captions(output_dir / "captions.tex", data)
    write_plots(output_dir, samples, data, dpi)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate PRG32 paper-ready LaTeX tables and high-resolution charts."
    )
    parser.add_argument("metrics_json", help="JSON downloaded from /api/performance.json")
    parser.add_argument("--out", default="paper_metrics", help="output directory")
    parser.add_argument("--dpi", type=int, default=300, help="chart resolution")
    args = parser.parse_args()

    process_metrics(Path(args.metrics_json), Path(args.out), args.dpi)


if __name__ == "__main__":
    main()
