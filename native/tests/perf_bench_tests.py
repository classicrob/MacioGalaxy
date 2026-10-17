#!/usr/bin/env python3
"""perf_bench.py analysis: gameplay-window stats, slow-frame phase breakdown, present columns, A/B noise verdicts.
No app runs."""
import csv
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import perf_bench as pb  # noqa: E402

PARTS = ["seam_compose", "seam_end_frame", "seam_begin_frame", "seam_reacquire", "retrace_wait", "game_work",
         "draw_done_wait", "unattributed", "retrace_wake", "pipeline_wait", "efb_staging_wait", "efb_submit_wait",
         "efb_capture_max", "drawable_acquire", "frame_submit", "present_call", "vi_timer_late_max",
         "vi_lock_wait_max", "texture_hash", "texture_upload", "token_barrier_wait", "stage_prep_wait"]


def rows(frames, extra=None):
    header = ["frame", "phase", "unfocused", "interval_ms", "efb_captures", "pipeline_resolves", "texture_uploads"]
    header += [p + "_ms" for p in PARTS] + list(extra or [])
    out = io.StringIO()
    writer = csv.writer(out)
    writer.writerow(header)
    for index, (interval, parts, more) in enumerate(frames, 1):
        values = {p: 0.0 for p in PARTS}
        values.update(parts)
        writer.writerow([index, "gameplay", 0, interval, 0, 0, 0] + [values[p] for p in PARTS] + list(more))
    return list(csv.DictReader(io.StringIO(out.getvalue())))


class WindowStats(unittest.TestCase):
    def test_thresholds_and_dominant_phase(self):
        normal = (16.68, {"retrace_wait": 10.0, "game_work": 6.0}, [])
        frames = [normal] * 96 + [
            (25.0, {"game_work": 20.0, "token_barrier_wait": 2.0, "retrace_wait": 1.0}, []),
            (40.0, {"game_work": 30.0, "draw_done_wait": 5.0}, []),
            (22.0, {"game_work": 4.0, "draw_done_wait": 16.0}, []),
            (16.9, {"game_work": 16.0}, []),
        ]
        stats = pb.window_stats(rows(frames), 0, 100)
        self.assertEqual(stats["frames"], 100)
        self.assertEqual(stats["over_16.7"], 4)
        self.assertEqual(stats["over_20.8"], 3)
        self.assertEqual(stats["over_33.3"], 1)
        self.assertEqual(stats["slow_frames"], 3)
        self.assertEqual(stats["dominant"], "game_cpu")  # 18 + 30 + 4 = 52 vs draw-done 21
        self.assertAlmostEqual(stats["slow_mean_ms"]["token_barrier"], 0.67, places=2)
        self.assertEqual(stats["max"], 40.0)

    def test_window_bounds_exclude_loading(self):
        frames = [(200.0, {}, [])] * 5 + [(16.68, {}, [])] * 10
        stats = pb.window_stats(rows(frames), 5, 15)
        self.assertEqual(stats["frames"], 10)
        self.assertEqual(stats["max"], 16.68)
        self.assertEqual(stats["dominant"], "-")

    def test_present_timestamps_become_intervals(self):
        frames = [(16.68, {}, [100000.0 + 16.68 * i]) for i in range(50)]
        frames[30] = (16.68, {}, [100000.0 + 16.68 * 30 + 10.0])
        stats = pb.window_stats(rows(frames, ["present_ms"]), 0, 50)
        self.assertEqual(stats["present"]["column"], "present_ms")
        self.assertEqual(stats["present"]["over_20.8"], 1)


class Compare(unittest.TestCase):
    def result(self, label, stage, repeat, p99, late):
        return {"label": label, "stage": stage, "scenario": 1, "repeat": repeat,
                "window": {"p50": 16.7, "p99": p99, "max": p99 + 5, "over_20.8": late, "over_33.3": 0}}

    def test_noise_and_verdicts(self):
        a = [self.result("A", "X", 1, 30.0, 40), self.result("A", "X", 2, 31.0, 44),
             self.result("A", "Y", 1, 20.0, 10), self.result("A", "Y", 2, 20.5, 12)]
        b = [self.result("B", "X", 1, 18.0, 2), self.result("B", "X", 2, 18.5, 3),
             self.result("B", "Y", 1, 20.2, 11), self.result("B", "Y", 2, 20.4, 11)]
        self.assertEqual(pb.noise(a)["p99"], 0.75)
        table = pb.compare_table(a, b)
        self.assertIn("| X-s1 | 2/2 |", table)
        x_line = next(l for l in table.splitlines() if l.startswith("| X-s1"))
        y_line = next(l for l in table.splitlines() if l.startswith("| Y-s1"))
        self.assertIn("p99 better", x_line)
        self.assertIn("within noise", y_line)


if __name__ == "__main__":
    unittest.main()
