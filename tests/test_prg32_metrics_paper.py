from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.prg32_metrics_paper import process_metrics


class MetricsPaperToolTests(unittest.TestCase):
    def test_process_onboard_performance_json(self) -> None:
        payload = {
            "ok": True,
            "run_id": "perf-test",
            "board_id": "board-1",
            "target": "esp32c6",
            "display_backend": "ili9341",
            "firmware_version": "test",
            "firmware_git_sha": "abc123",
            "game_name": "setup-performance-test",
            "build_type": "release",
            "wifi_mode": "access_point",
            "screen_count": 1,
            "aggregate_windows": [
                {
                    "window_index": 0,
                    "frames": 2,
                    "fps_mean": 40.0,
                    "frame_us_mean": 25000,
                    "frame_us_p95": 26000,
                    "frame_us_p99": 26000,
                    "missed_deadlines": 0,
                }
            ],
            "screen_summaries": [
                {
                    "screen_index": 0,
                    "screen_name": "clear-fill",
                    "metric_goal": "viewport clear and large rectangle fill bandwidth",
                    "frames": 2,
                    "fps_mean": 40.0,
                    "frame_us_mean": 25000,
                    "frame_us_p95": 26000,
                    "frame_us_p99": 26000,
                    "missed_deadlines": 0,
                }
            ],
            "samples": [
                {
                    "frame_index": 0,
                    "screen_index": 0,
                    "screen_name": "clear-fill",
                    "t_update_us": 500,
                    "t_draw_us": 7000,
                    "t_present_us": 17000,
                    "t_frame_total_us": 24500,
                    "deadline_missed": False,
                    "free_heap_bytes": 123000,
                    "min_free_heap_bytes": 122000,
                    "input_mask": 0,
                    "upload_queue_depth": 0,
                },
                {
                    "frame_index": 1,
                    "screen_index": 0,
                    "screen_name": "clear-fill",
                    "t_update_us": 520,
                    "t_draw_us": 7100,
                    "t_present_us": 18000,
                    "t_frame_total_us": 25620,
                    "deadline_missed": False,
                    "free_heap_bytes": 122900,
                    "min_free_heap_bytes": 122000,
                    "input_mask": 0,
                    "upload_queue_depth": 0,
                },
            ],
        }

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "metrics.json"
            out_dir = root / "paper"
            source.write_text(json.dumps(payload), encoding="utf-8")
            with patch.dict("os.environ", {"PRG32_METRICS_SKIP_PLOTS": "1"}):
                process_metrics(source, out_dir, 300)

            self.assertTrue((out_dir / "samples.csv").exists())
            self.assertTrue((out_dir / "table_summary.tex").exists())
            self.assertTrue((out_dir / "table_windows.tex").exists())
            self.assertTrue((out_dir / "table_screens.tex").exists())
            self.assertTrue((out_dir / "captions.tex").exists())
            self.assertIn("Mean FPS", (out_dir / "table_summary.tex").read_text())
            self.assertIn("clear-fill", (out_dir / "table_screens.tex").read_text())


if __name__ == "__main__":
    unittest.main()
