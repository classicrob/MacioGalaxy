"""Live wrapper transport, cancellation and verdict handling without starting a game."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from petari_level import Live, LevelError


class LiveTests(unittest.TestCase):
    def test_start_reads_directory_after_fixture_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/"cmd").mkdir(); (Path(directory)/"reply").mkdir()
            output = SimpleNamespace(returncode=0, stdout=f"Created fixture. Source NAND only read.\n{directory}\nready in 30 s\n", stderr="")
            with patch("petari_level.live.subprocess.run", return_value=output):
                self.assertEqual(Live.start("fake-app").dir, Path(directory).resolve())

    def test_structured_live_error_is_raised(self):
        with tempfile.TemporaryDirectory() as directory, patch("petari_level.live._send", return_value='error {"code":"busy","message":"running","hint":"cancel","where":"inputs"}'):
            with self.assertRaises(LevelError) as caught:
                Live(directory).send("inputs-start request.json")
            self.assertEqual(caught.exception.code, "busy")

    def test_legacy_live_error_is_raised(self):
        with tempfile.TemporaryDirectory() as directory, patch("petari_level.live._send", return_value="error old server refused"):
            with self.assertRaises(LevelError) as caught:
                Live(directory).send("inputs-start request.json")
            self.assertEqual(caught.exception.code, "live")

    def test_route_payload_handles_spaces_and_preserves_star_verdict(self):
        with tempfile.TemporaryDirectory(prefix="petari live ") as directory:
            live = Live(directory)
            def send(command, timeout=120):
                if command.startswith("verify-route "):
                    request = json.loads(Path(command[len("verify-route "):]).read_text())
                    self.assertTrue(request["require_star"])
                    self.assertEqual(request["frames"], 18000)
                    return {"active":True}
                return {"active":False,"finished":True,"passed":True,"reason":"star and return","frame":3145}
            live.send = send
            result = live.run_route("course with spaces.csv")
            self.assertTrue(result["passed"])
            self.assertEqual(result["frame"], 3145)
            self.assertFalse(list(Path(directory).glob("playback-*")))

    def test_external_route_cancel_is_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Live(directory)
            live.send = lambda *args, **kwargs: {"active":False,"finished":False,"passed":False}
            result = live.run_route("course.csv")
            self.assertFalse(result["passed"])
            self.assertEqual(result["reason"], "route cancelled")

    def test_input_timeout_cancels_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Live(directory)
            commands = []
            def send(command, **kwargs):
                commands.append(command)
                return {"active":True}
            live.send = send
            with self.assertRaises(LevelError) as caught:
                live.run_inputs([{"frames":30,"stick":[0,1]}], timeout=-1)
            self.assertEqual(caught.exception.code, "timeout")
            self.assertEqual(commands[-1], "inputs-cancel")

    def test_sync_refuses_different_base_before_mutating(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Live(directory)
            observed = []
            live.begin = lambda: observed.append("begin")
            live.state = lambda: {"editor":{"galaxy":"EggStarGalaxy","mission":1,"base_fingerprint":"other","cursor":0,"history":0}}
            level = SimpleNamespace(info=lambda: {"galaxy":"EggStarGalaxy","mission":1,"base_fingerprint":"frozen"},
                                    history=lambda: self.fail("mismatch must stop before export"))
            with self.assertRaises(LevelError) as caught:
                live.sync(level)
            self.assertEqual(caught.exception.code, "base-mismatch")
            self.assertFalse(live.sync_file.exists())

    def test_sync_fresh_server_discards_previous_process_delivery(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Live(directory)
            live.sync_file.write_text('["old-op"]')
            live.begin = lambda: None
            live.state = lambda: {"editor":{"galaxy":"EggStarGalaxy","mission":1,"base_fingerprint":"frozen","cursor":0,"history":0}}
            sent = []
            live.apply_ops = lambda ops: sent.extend(ops) or {}
            live.undo = lambda *args: self.fail("fresh game must not undo old process ops")
            level = SimpleNamespace(info=lambda: {"galaxy":"EggStarGalaxy","mission":1,"base_fingerprint":"frozen"},
                                    history=lambda: {"cursor":1}, export=lambda *args: [{"label":"new"}])
            self.assertEqual(live.sync(level)["applied"], 1)
            self.assertEqual(sent, [{"label":"new"}])

    def test_sync_refuses_untracked_ui_history(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Live(directory)
            live.begin = lambda: None
            live.state = lambda: {"editor":{"galaxy":"EggStarGalaxy","mission":1,"base_fingerprint":"frozen","cursor":1,"history":1}}
            level = SimpleNamespace(info=lambda: {"galaxy":"EggStarGalaxy","mission":1,"base_fingerprint":"frozen"},
                                    history=lambda: {"cursor":0}, export=lambda *args: [])
            with self.assertRaises(LevelError) as caught:
                live.sync(level)
            self.assertEqual(caught.exception.code, "base-mismatch")

    def test_nan_is_rejected_before_request_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Live(directory)
            with self.assertRaises(ValueError):
                live.start_inputs([{"frames":3,"stick":[float("nan"),0]}])
            self.assertFalse(list(Path(directory).glob("*.json")))

    def test_plain_screenshot_sends_only_the_path(self):
        with tempfile.TemporaryDirectory() as directory:
            live, sent = Live(directory), []
            live.send = lambda command, timeout=120: sent.append(command) or {}
            live.screenshot(Path(directory) / "shot.png")
            self.assertEqual(sent, [f"screenshot {(Path(directory) / 'shot.png').resolve()}"])

    def test_posed_screenshot_sends_eye_target_up_and_fovy(self):
        with tempfile.TemporaryDirectory() as directory:
            live, sent = Live(directory), []
            live.send = lambda command, timeout=120: sent.append(command) or {}
            live.screenshot(Path(directory) / "shot.png", eye=[1, 2.5, -3], target=(0, 0, 0), up=[0, 1, 0], fovy=60)
            self.assertTrue(sent[0].endswith(" eye 1 2.5 -3 target 0 0 0 up 0 1 0 fovy 60"), sent[0])

    def test_posed_screenshot_needs_eye_and_target(self):
        with tempfile.TemporaryDirectory() as directory:
            live, sent = Live(directory), []
            live.send = lambda command, timeout=120: sent.append(command) or {}
            with self.assertRaises(ValueError):
                live.screenshot(Path(directory) / "shot.png", eye=[1, 2, 3])
            self.assertEqual(sent, [])


if __name__ == "__main__":
    unittest.main()
