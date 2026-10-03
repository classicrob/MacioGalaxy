"""Live mode: the same edits applied to a running game through the test server (AGENT_API_X.md "Live mode").

    from level_api_x import Level, Live
    live = Live.start(app="build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari", stage="EggStarGalaxy")
    live.begin()                       # the edit model in the running game
    live.sync(lv)                      # the workspace's batches -> the game (undo/redo follow)
    live.reload()                      # rebuild the scene from the model (~0.4 s)
    live.play(pos=[0, 1500, 0])        # Mario there, playing
    live.wait(120); print(live.mario())
    live.screenshot("build/shots/arc.png")
    live.stop()

Replies are the test server's "ok <json>" lines, parsed; "error ..." raises LevelError (code "live").
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Optional, Sequence

from .level import Level, LevelError

TOOLS = Path(__file__).resolve().parents[1]


def _send(directory: Path, command: str, timeout: float) -> str:
    name = f"{time.time_ns()}"
    tmp = directory / "cmd" / f"{name}.tmp"
    tmp.write_text(command + "\n")
    tmp.rename(directory / "cmd" / f"{name}.txt")
    reply = directory / "reply" / f"{name}.txt"
    deadline = time.time() + timeout
    while time.time() < deadline:
        if reply.is_file():
            text = reply.read_text().strip()
            if text:
                return text
        time.sleep(0.03)
    return f"error no reply within {timeout:.0f} s"


class Live:
    """A running Petari with PETARI_TEST_SERVER (and PETARI_EDITOR=1 for editing)."""

    def __init__(self, directory: str | os.PathLike):
        self.dir = Path(directory).resolve()
        self.sync_file = self.dir / "live-sync.json"

    @classmethod
    def start(cls, app: str | os.PathLike, *, stage: str = "EggStarGalaxy", scenario: int = 1,
              directory: Optional[str | os.PathLike] = None, owner: str = "level-api-x") -> "Live":
        """Boot a test server (editor on) and return when the stage answers."""
        argv = [sys.executable, str(TOOLS / "test_server.py"), "start", "--app", str(app), "--stage", stage,
                "--scenario", str(scenario), "--owner", owner]
        if directory:
            argv += ["--dir", str(directory)]
        env = dict(os.environ, PETARI_EDITOR="1")
        out = subprocess.run(argv, capture_output=True, text=True, env=env)
        if out.returncode != 0:
            raise LevelError({"code": "live", "message": "test server did not start", "hint": out.stdout + out.stderr})
        return cls(out.stdout.splitlines()[0].strip())

    def stop(self) -> None:
        subprocess.run([sys.executable, str(TOOLS / "test_server.py"), "stop", "--dir", str(self.dir)], check=False)

    def send(self, command: str, timeout: float = 120) -> Any:
        text = _send(self.dir, command, timeout)
        if text.startswith("error"):
            raise LevelError({"code": "live", "message": text[6:], "where": command.split()[0]})
        body = text[3:] if text.startswith("ok ") else ""
        try:
            return json.loads(body)
        except ValueError:
            return {"text": body}

    # ---- editing ------------------------------------------------------------------------------

    def begin(self) -> dict:
        """Load the editor's model for the running stage (no Edit nerve needed)."""
        return self.send("editor-begin")

    def state(self) -> dict:
        return self.send("editor-state")

    def apply_ops(self, ops: Sequence[dict]) -> dict:
        """Low-level ops (petari-level-x export) applied in order, each one undo step in the game."""
        path = self.dir / f"ops-{time.time_ns()}.json"
        path.write_text(json.dumps({"ops": list(ops)}))
        try:
            return self.send(f"editor-apply {path}")
        finally:
            path.unlink(missing_ok=True)

    def undo(self, steps: int = 1) -> dict:
        return self.send(f"editor-undo {steps}")

    def redo(self, steps: int = 1) -> dict:
        return self.send(f"editor-redo {steps}")

    def sync(self, level: Level) -> dict:
        """Bring the game to the workspace's journal: undo what diverged, apply what is new."""
        history = level.history()
        ops = level.export(0, history["cursor"])
        want = [hashlib.sha1(json.dumps(op, sort_keys=True).encode()).hexdigest() for op in ops]
        sent = json.loads(self.sync_file.read_text()) if self.sync_file.exists() else []
        common = 0
        while common < min(len(sent), len(want)) and sent[common] == want[common]:
            common += 1
        result: dict = {"undone": len(sent) - common, "applied": len(want) - common}
        if len(sent) > common:
            self.undo(len(sent) - common)
        if len(want) > common:
            result["reply"] = self.apply_ops(ops[common:])
        self.sync_file.write_text(json.dumps(want))
        return result

    def reload(self) -> dict:
        return self.send("editor-reload")

    def save(self, mod: str, *, full: bool = False) -> dict:
        return self.send(f"editor-save {mod}" + (" --full" if full else ""))

    def checks(self) -> list:
        return self.send("editor-checks")

    # ---- playing and looking ------------------------------------------------------------------

    def play(self, pos: Optional[Sequence[float]] = None, up: Sequence[float] = (0, 1, 0),
             front: Sequence[float] = (0, 0, 1)) -> dict:
        """Play from a marker at pos (Mario teleported there, upright for up), reloading first if edits are pending."""
        if pos is None:
            return self.send("editor-play")
        words = " ".join(f"{v:.2f}" for v in [*pos, *up, *front])
        return self.send(f"editor-play {words}")

    def teleport(self, pos: Sequence[float]) -> dict:
        """Mario to pos right now (no editor involved); returns Mario two frames later."""
        return self.send("teleport " + " ".join(f"{v:.2f}" for v in pos))

    def trace(self, frames: int, every: int = 6) -> list:
        """Mario's positions while the game runs `frames` frames (one sample per `every`)."""
        out = []
        for _ in range(max(1, frames // every)):
            m = self.wait(every).get("mario", {})
            if m.get("present"):
                out.append(m)
        return out

    def mario(self) -> dict:
        return self.send("mario")

    def wait(self, frames: int) -> dict:
        """Let the game run; returns Mario afterwards."""
        return self.send(f"wait {frames}", timeout=max(30, frames / 20))

    def ground(self, pos: Sequence[float], down: Optional[Sequence[float]] = None) -> dict:
        """The engine's own gravity and first map polygon along it (exact)."""
        words = " ".join(f"{v:.2f}" for v in [*pos, *(down or [])])
        return self.send(f"ground {words}")

    def gravity(self, pos: Sequence[float]) -> dict:
        return self.send("gravity " + " ".join(f"{v:.2f}" for v in pos))

    def screenshot(self, path: str | os.PathLike) -> dict:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        return self.send(f"screenshot {Path(path).resolve()}")
