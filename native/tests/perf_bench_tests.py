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
    def test_thresholds_r2r_busy_and_late_phase(self):
        normal = (16.68, {"retrace_wait": 10.0, "game_work": 6.0}, [])
        # A consistent timeline (retraces every 16.68 ms). Row 96's work (30 ms) misses one retrace: row 97 waits
        # for the one after (r2r 33.36). Row 98 is seam-to-seam jitter only: 15 ms of work, the next retrace on time.
        frames = [normal] * 96 + [
            (40.0, {"retrace_wait": 10.0, "game_work": 30.0}, []),
            (10.04, {"retrace_wait": 3.36, "game_work": 6.68}, []),
            (25.0, {"retrace_wait": 10.0, "game_work": 15.0}, []),
            (8.36, {"retrace_wait": 1.68, "game_work": 6.68}, []),
        ]
        stats = pb.window_stats(rows(frames), 0, 100)
        self.assertEqual(stats["frames"], 100)
        self.assertEqual(stats["over_20.8"], 2)          # raw intervals 40 and 25
        self.assertEqual(stats["over_33.3"], 1)
        r2r = stats["r2r"]
        self.assertEqual(r2r["n"], 99)
        self.assertEqual(r2r["over_20.8"], 1)             # only the real overrun
        self.assertAlmostEqual(r2r["max"], 33.36, places=2)
        self.assertEqual(stats["late"]["basis"], "r2r > 20.8")
        self.assertEqual(stats["dominant"], "game_cpu")   # row 96's 30 ms of game work
        self.assertAlmostEqual(stats["busy"]["max"], 30.0, places=2)

    def test_window_bounds_exclude_loading(self):
        frames = [(200.0, {}, [])] * 5 + [(16.68, {"retrace_wait": 10.0}, [])] * 10
        stats = pb.window_stats(rows(frames), 5, 15)
        self.assertEqual(stats["frames"], 10)
        self.assertEqual(stats["max"], 16.68)
        self.assertEqual(stats["dominant"], "-")
        self.assertEqual(stats["r2r"]["max"], 16.68)

    def test_present_columns(self):
        frames = [(16.68, {}, ["16.67", str(i), "ok"]) for i in range(40)]
        frames += [(16.68, {}, ["", "", "dropped"])] * 9 + [(16.68, {}, ["33.33", "50", "ok"])]
        stats = pb.window_stats(rows(frames, ["present_ms", "presented_frame", "present_state"]), 0, 50)
        self.assertEqual(stats["present"]["states"], {"ok": 41, "dropped": 9})
        self.assertEqual(stats["present"]["over_20.8"], 1)
        self.assertEqual(stats["present"]["p50"], 16.67)
        ticked = pb.window_stats(rows(frames, ["present_ms", "presented_frame", "present_state"]), 0, 50, 120.0)
        self.assertEqual(ticked["present"]["ticks_1_2_3_4plus"], [0, 40, 0, 1])
        self.assertEqual(ticked["present"]["off_cadence"], 0)


class ExitLine(unittest.TestCase):
    def test_present_exit_line(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / "app.log").write_text(
                "Petari present intervals (on-screen, 1200): p50 16.67 p95 16.70 p99 25.00 max 50.01 ms; off-cadence "
                "(not within 2 ms of a multiple of the refresh period) 12 (1.0%); intervals of 1/2/3/4+ refreshes "
                "10/1150/30/10; display 120.00 Hz; present mode Fifo; drawables 1500, presented 1210, dropped 280, "
                "never presented 3, out of order 0\n")
            result = pb.analyze_run(run, run / "user", "EggStarGalaxy", 0, False)
        e = result["present_exit"]
        self.assertEqual((e["n"], e["p99"], e["mode"], e["dropped"]), (1200, 25.0, "Fifo", 280))
        self.assertEqual(e["ticks_1_2_3_4plus"], [10, 1150, 30, 10])


class Compare(unittest.TestCase):
    def result(self, label, stage, repeat, p99, late):
        return {"label": label, "stage": stage, "scenario": 1, "repeat": repeat,
                "window": {"p99": p99, "r2r": {"p99": p99, "over_20.8": late, "max": p99 + 5},
                           "busy": {"p99": 10.0}}}

    def test_noise_and_verdicts(self):
        a = [self.result("A", "X", 1, 30.0, 40), self.result("A", "X", 2, 31.0, 44),
             self.result("A", "Y", 1, 20.0, 10), self.result("A", "Y", 2, 20.5, 12)]
        b = [self.result("B", "X", 1, 18.0, 2), self.result("B", "X", 2, 18.5, 3),
             self.result("B", "Y", 1, 20.2, 11), self.result("B", "Y", 2, 20.4, 11)]
        self.assertEqual(pb.noise(a)["r2r_p99"], 0.75)
        table = pb.compare_table(a, b)
        x_line = next(l for l in table.splitlines() if l.startswith("| X-s1"))
        y_line = next(l for l in table.splitlines() if l.startswith("| Y-s1"))
        self.assertIn("r2r_p99 better", x_line)
        self.assertIn("within noise", y_line)


class Spikes(unittest.TestCase):
    def test_spikes_with_events(self):
        normal = (16.68, {"retrace_wait": 10.0, "game_work": 6.0}, [])
        frames = [normal] * 20 + [
            (40.0, {"retrace_wait": 10.0, "game_work": 30.0}, []),   # row 21: overruns
            (10.04, {"retrace_wait": 3.36, "game_work": 6.68}, []),  # row 22: the late retrace
        ] + [normal] * 10
        data = rows(frames)
        data[29]["phase"] = "loading"
        data[29]["interval_ms"] = "120.0"
        data[29]["stage_prep_wait_ms"] = "100.0"
        data[29]["game_work_ms"] = "101.0"
        log = "PETARI SMOKE [frame 15]: planet Peanut: collecting star chips\nPETARI SMOKE [frame 25]: PowerStar.Get\n"
        result = pb.spike_list(data, 1, pb.events_from_log(log))
        worst = result["worst"]
        self.assertEqual(worst[0]["kind"], "long frame")
        self.assertEqual(worst[0]["frame"], 30)
        self.assertIn("stage_prep 100.0", worst[0]["cause"])
        self.assertEqual(worst[0]["before"], "-5f PowerStar.Get")
        late = next(s for s in worst if s["kind"] == "late retrace")
        self.assertEqual(late["frame"], 22)
        self.assertIn("game_cpu 30.0", late["cause"])
        self.assertEqual(late["before"], "-7f planet Peanut: collecting star chips")
        self.assertEqual(late["after"], "+3f PowerStar.Get")
        self.assertEqual(result["counts"], {"long frame": 2, "late retrace": 1})


class CleanGuard(unittest.TestCase):
    def test_refuses_while_busy(self):
        import fcntl, os, tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as tmp:
            locks, warm = Path(tmp) / "build", Path(tmp) / "warm"
            locks.mkdir()
            (warm / "apps").mkdir(parents=True)
            self.assertEqual(pb.busy_reasons(locks, warm, listing=""), [])
            # A live app-lock holder (this process), a dead one (ignored) and a live ticket.
            held = locks / ".petari-app.lock"
            held.mkdir()
            (held / "pid").write_text(str(os.getpid()))
            (held / "owner").write_text("frame-cost")
            dead = locks / ".petari-app.lock.s2"
            dead.mkdir()
            (dead / "pid").write_text("999999")
            queue = locks / ".petari-app.lock.queue"
            queue.mkdir()
            (queue / f"123-{os.getpid()}").write_text("lead quiet")
            reasons = pb.busy_reasons(locks, warm, listing="")
            self.assertEqual(len(reasons), 2, reasons)
            self.assertIn("held by frame-cost", reasons[0])
            # Inside its own batched lock, holders and tickets are this bench's own.
            self.assertEqual(pb.busy_reasons(locks, warm, listing="", own_lock=True), [])
            # A held metal.lock and a running bench app count even then.
            with open(warm / "metal.lock", "w") as handle:
                fcntl.flock(handle, fcntl.LOCK_EX)
                app = f"{warm}/apps/abc/Petari.app/Contents/MacOS/Petari --disc x"
                reasons = pb.busy_reasons(locks, warm, listing=f"4242 {app}\n77 /bin/zsh", own_lock=True)
            self.assertEqual(len(reasons), 2, reasons)
            self.assertIn("metal.lock held", reasons[0])
            self.assertIn("pid 4242", reasons[1])
            self.assertEqual(pb.busy_reasons(locks, warm, listing="", own_lock=True), [])


if __name__ == "__main__":
    unittest.main()
