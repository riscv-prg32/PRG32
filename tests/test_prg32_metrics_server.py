from __future__ import annotations

import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from tools.prg32_metrics_server.app import create_app
from tools.prg32_metrics_server.export_run import export_run


class MetricsServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp.name) / "metrics.sqlite"
        self.app = create_app(self.db_path)
        self.app.config.update(TESTING=True)
        self.client = self.app.test_client()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def post_run(self, run_id: str = "test-run") -> None:
        response = self.client.post(
            "/api/runs",
            json={
                "run_id": run_id,
                "board_id": "board-1",
                "target": "esp32c6",
                "display_backend": "ili9341",
                "firmware_version": "test",
                "firmware_git_sha": "abc123",
                "game_name": "pong",
                "sample_period_frames": 1,
                "started_ms": 1000,
            },
        )
        self.assertEqual(response.status_code, 200, response.get_json())

    def test_run_and_batch_round_trip(self) -> None:
        self.post_run()
        response = self.client.post(
            "/api/metrics/batch",
            json={
                "run_id": "test-run",
                "dropped_samples": 1,
                "samples": [
                    {
                        "frame": 1,
                        "timestamp_ms": 1033,
                        "update_us": 500,
                        "draw_us": 6000,
                        "present_us": 17000,
                        "frame_us": 23500,
                        "heap_free": 123456,
                        "heap_min_free": 120000,
                        "input_mask": 0,
                        "fps_x100": 4255,
                        "upload_queue_depth": 1,
                        "deadline_missed": False,
                    }
                ],
            },
        )
        self.assertEqual(response.status_code, 200, response.get_json())
        self.assertEqual(response.get_json()["inserted"], 1)

        duplicate = self.client.post(
            "/api/metrics/batch",
            json={"run_id": "test-run", "samples": [{"frame": 1}]},
        )
        self.assertEqual(duplicate.status_code, 200, duplicate.get_json())
        self.assertEqual(duplicate.get_json()["inserted"], 0)

        detail = self.client.get("/api/runs/test-run")
        self.assertEqual(detail.status_code, 200, detail.get_json())
        self.assertEqual(detail.get_json()["summary"]["sample_count"], 1)

        csv_response = self.client.get("/api/runs/test-run/samples.csv")
        self.assertEqual(csv_response.status_code, 200)
        self.assertIn("frame_us", csv_response.text)
        self.assertIn("23500", csv_response.text)

    def test_missing_run_is_rejected_for_batch(self) -> None:
        response = self.client.post(
            "/api/metrics/batch",
            json={"run_id": "missing", "samples": []},
        )
        self.assertEqual(response.status_code, 404)

    def test_markdown_report_and_finish(self) -> None:
        self.post_run()
        finish = self.client.post(
            "/api/runs/test-run/finish",
            json={"finished_ms": 2000, "dropped_samples": 2},
        )
        self.assertEqual(finish.status_code, 200, finish.get_json())

        report = self.client.get("/api/runs/test-run/report.md")
        self.assertEqual(report.status_code, 200)
        self.assertIn("# PRG32 Metrics Report", report.text)
        self.assertIn("Dropped samples: 2", report.text)

    def test_export_run_writes_report_files(self) -> None:
        self.post_run()
        self.client.post(
            "/api/metrics/batch",
            json={
                "run_id": "test-run",
                "samples": [
                    {
                        "frame": 1,
                        "timestamp_ms": 1033,
                        "update_us": 500,
                        "draw_us": 6000,
                        "present_us": 17000,
                        "frame_us": 23500,
                        "heap_free": 123456,
                        "heap_min_free": 120000,
                        "input_mask": 0,
                        "fps_x100": 4255,
                        "upload_queue_depth": 1,
                        "deadline_missed": False,
                    }
                ],
            },
        )
        out_dir = Path(self.tmp.name) / "export"
        with patch.dict("os.environ", {"PRG32_METRICS_SKIP_PLOTS": "1"}):
            export_run(self.db_path, "test-run", out_dir)
        self.assertTrue((out_dir / "metadata.json").exists())
        self.assertTrue((out_dir / "samples.csv").exists())
        self.assertTrue((out_dir / "summary.csv").exists())
        self.assertTrue((out_dir / "table_summary.tex").exists())
        self.assertIn("PRG32 Metrics Report", (out_dir / "report.md").read_text())


if __name__ == "__main__":
    unittest.main()
