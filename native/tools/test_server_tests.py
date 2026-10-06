#!/usr/bin/env python3
"""Unit tests for test_server.py: stop is safe after a successful quit, with a reused pid, and still kills a server that ignores
quit; a fresh start empties the command and reply queues."""
import argparse
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_server as ts  # noqa: E402


class Stop(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        (self.dir / "user/cache").mkdir(parents=True)
        self.send, ts.send = ts.send, lambda *a, **k: "error no reply"
        self.wait, ts.QUIT_WAIT_SECONDS = ts.QUIT_WAIT_SECONDS, 1

    def tearDown(self):
        ts.send, ts.QUIT_WAIT_SECONDS = self.send, self.wait

    def stop(self):
        return ts.cmd_stop(argparse.Namespace(dir=str(self.dir)))

    def test_pid_already_gone(self):
        (self.dir / "server.pid").write_text("999999")
        self.assertEqual(self.stop(), 0)
        self.assertFalse((self.dir / "user/cache").exists())
        self.assertFalse((self.dir / "server.pid").exists())

    def test_pid_reused_by_a_process_that_is_not_ours(self):
        (self.dir / "server.pid").write_text("1")  # launchd: signalling it raises PermissionError
        self.assertEqual(self.stop(), 0)
        self.assertFalse((self.dir / "server.pid").exists())

    def test_no_pid_file(self):
        self.assertEqual(self.stop(), 0)

    def test_a_server_that_ignores_quit_is_terminated(self):
        proc = subprocess.Popen(["sleep", "60"], start_new_session=True)
        (self.dir / "server.pid").write_text(str(proc.pid))
        self.assertEqual(self.stop(), 0)
        proc.wait(timeout=5)
        self.assertIsNotNone(proc.returncode)


class Queues(unittest.TestCase):
    def test_a_fresh_start_drops_commands_and_replies_left_by_a_previous_server(self):
        directory = Path(tempfile.mkdtemp())
        (directory / "cmd").mkdir()
        (directory / "reply").mkdir()
        (directory / "cmd/1.txt").write_text("quit\n")       # a send() that timed out: the old server never read it
        (directory / "cmd/2.tmp").write_text("warp X\n")
        (directory / "reply/3.txt").write_text("ok old\n")
        (directory / "keep.txt").write_text("not a queue")
        ts.reset_queues(directory)
        self.assertEqual(list((directory / "cmd").iterdir()), [])
        self.assertEqual(list((directory / "reply").iterdir()), [])
        self.assertTrue((directory / "keep.txt").exists())

    def test_missing_queues_are_created(self):
        directory = Path(tempfile.mkdtemp()) / "new"
        ts.reset_queues(directory)
        self.assertTrue((directory / "cmd").is_dir() and (directory / "reply").is_dir())


if __name__ == "__main__":
    unittest.main()
