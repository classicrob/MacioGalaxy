"""Live mode: the same edits applied to a running game through the test server (AGENT_API.md "Live mode").

    from petari_level import Level, Live
    live = Live.start(app="build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari", stage="EggStarGalaxy")
    live.begin()                       # the edit model in the running game
    live.sync(lv)                      # the workspace's batches -> the game (undo/redo follow)
    live.reload()                      # rebuild the scene from the model (~0.4 s)
    live.play(pos=[0, 1500, 0])        # Mario there, playing
    live.wait(120); print(live.mario())
    live.screenshot("build/shots/arc.png")
    live.stop()

Replies are the test server's "ok <json>" lines, parsed; "error <json Error>" raises LevelError (legacy text errors use code "live").
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
              directory: Optional[str | os.PathLike] = None, owner: str = "petari-level") -> "Live":
        """Boot a test server (editor on) and return when the stage answers."""
        argv = [sys.executable, str(TOOLS / "test_server.py"), "start", "--app", str(app), "--stage", stage,
                "--scenario", str(scenario), "--owner", owner]
        if directory:
            argv += ["--dir", str(directory)]
        env = dict(os.environ, PETARI_EDITOR="1")
        out = subprocess.run(argv, capture_output=True, text=True, env=env)
        if out.returncode != 0:
            raise LevelError({"code": "live", "message": "test server did not start", "hint": out.stdout + out.stderr})
        if directory:
            return cls(directory)
        for line in reversed(out.stdout.splitlines()):
            candidate = Path(line.strip())
            try:
                if (candidate / "cmd").is_dir() and (candidate / "reply").is_dir():
                    return cls(candidate)
            except OSError:
                continue
        raise LevelError({"code": "live", "message": "Started server did not report its command directory.", "hint": out.stdout})

    def stop(self) -> None:
        subprocess.run([sys.executable, str(TOOLS / "test_server.py"), "stop", "--dir", str(self.dir)], check=False)

    def send(self, command: str, timeout: float = 120) -> Any:
        text = _send(self.dir, command, timeout)
        if text.startswith("error"):
            try:
                error = json.loads(text[6:])
            except ValueError:
                error = None
            if isinstance(error, dict) and "code" in error and "message" in error:
                raise LevelError(error)
            raise LevelError({"code": "live", "message": text[6:], "where": command.split()[0]})
        body = text[3:] if text.startswith("ok ") else ""
        try:
            result = json.loads(body)
            if isinstance(result, dict) and result.get("ok") is False:
                raise LevelError(result["error"])
            return result
        except ValueError:
            return {"text": body}

    # ---- editing ------------------------------------------------------------------------------

    def begin(self) -> dict:
        """Load the editor's model for the running stage (no Edit nerve needed)."""
        return self.send("editor-begin")

    def state(self) -> dict:
        return self.send("editor-state")

    def apply_ops(self, ops: Sequence[dict]) -> dict:
        """Low-level ops (petari-level export) applied in order, each one undo step in the game."""
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
        info = level.info()
        self.begin()
        state = self.state().get("editor", {})
        expected = info.get("base_fingerprint")
        actual = state.get("base_fingerprint")
        if (not expected or expected != actual or info.get("galaxy") != state.get("galaxy")
                or info.get("mission") != state.get("mission")):
            raise LevelError({"code": "base-mismatch", "message": "The game and workspace have different loaded bases.",
                              "hint": "Enable the same input mods in the test server's user directory, restart the game, and use a server with base_fingerprint support.",
                              "where": "Live.sync", "candidates": []})
        history = level.history()
        ops = level.export(0, history["cursor"])
        want = [hashlib.sha1(json.dumps(op, sort_keys=True).encode()).hexdigest() for op in ops]
        sent = json.loads(self.sync_file.read_text()) if self.sync_file.exists() else []
        if state.get("cursor") == 0 and state.get("history") == 0:
            sent = []  # a fresh game process has not received the persisted sync journal
        elif state.get("cursor") != len(sent):
            raise LevelError({"code": "base-mismatch", "message": "The running game's journal has untracked edits.",
                              "hint": "Save any UI edits and restart from the matching base before syncing.", "where": "Live.sync"})
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

    def screenshot(self, path: str | os.PathLike, eye=None, target=None, up=None, fovy=None) -> dict:
        """The next presented frame as PNG. With eye and target (Edit mode), the editor camera moves there first."""
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        command = f"screenshot {Path(path).resolve()}"
        if eye is not None or target is not None:
            if eye is None or target is None:
                raise ValueError("a posed screenshot needs both eye and target")
            command += " eye " + " ".join(f"{float(v):g}" for v in eye) + " target " + " ".join(f"{float(v):g}" for v in target)
            if up is not None:
                command += " up " + " ".join(f"{float(v):g}" for v in up)
            if fovy is not None:
                command += f" fovy {float(fovy):g}"
        return self.send(command)


    def _start_plan(self, command: str, payload: Any) -> dict:
        path = self.dir / f"playback-{time.time_ns()}.json"
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(payload, allow_nan=False))
        tmp.rename(path)
        try:
            return self.send(f"{command} {path}")
        finally:
            path.unlink(missing_ok=True)

    def route_status(self) -> dict:
        """Current waypoint replay verdict: active, finished, passed, reason, frame."""
        return self.send("verify-route-status")

    def cancel_route(self) -> dict:
        """Release the replay's controls and cancel it."""
        return self.send("verify-route-cancel")

    def start_route(self, route: str | os.PathLike, *, require_star: bool = True, frames: int = 18000) -> dict:
        """Start the game's waypoint replay driver in normal Play.

        CSV rows are world x,y,z,action (Walk/Jump/Launch/etc.), as PETARI_ROUTE_RECORD
        produces. require_star=True requires Star.Get and a playable observatory
        return; False requires reaching the route end and landing for 30 frames.
        """
        return self._start_plan("verify-route", {"file": str(Path(route).resolve()),
                                "frames": frames, "require_star": require_star})

    def run_route(self, route: str | os.PathLike, *, require_star: bool = True,
                  frames: int = 18000, timeout: float = 600) -> dict:
        """Replay and return its beatability verdict; timeout releases controls.

        A pass verifies this recorded traversal, not every possible player route.
        A failed traversal returns passed=False with the driver's reason.
        """
        self.start_route(route, require_star=require_star, frames=frames)
        deadline = time.monotonic() + timeout
        try:
            while time.monotonic() < deadline:
                result = self.route_status()
                if result["finished"] or not result["active"]:
                    if not result["finished"]:
                        result.update(finished=True, passed=False, reason="route cancelled")
                    return result
                time.sleep(0.25)
            raise LevelError({"code": "timeout", "message": "Replay timed out; controls released."})
        except BaseException:
            self.cancel_route()
            raise

    def inputs_status(self) -> dict:
        """Exact frame delivery plus Mario's final position, velocity and ground/dead state."""
        return self.send("inputs-status")

    def cancel_inputs(self) -> dict:
        return self.send("inputs-cancel")

    def start_inputs(self, steps: Sequence[dict]) -> dict:
        """Start exact game-frame steps: frames, optional buttons and stick [right,forward].

        Buttons: forward/back/left/right, jump, action, spin, crouch, cappy,
        camera_left/right, plus, minus. Stick axes are -1..1; direction names
        cannot accompany stick. Cappy requires its enabled mod and binding.
        """
        return self._start_plan("inputs-start", list(steps))

    def run_inputs(self, steps: Sequence[dict], *, timeout: float = 120) -> dict:
        """Wait for exact input delivery, not a beatability verdict; timeout cancels.

        Engine completion is completed=True, frame==total. Death, demo, scene
        change or lost player ends early and releases every control.
        """
        self.start_inputs(steps)
        deadline = time.monotonic() + timeout
        try:
            while time.monotonic() < deadline:
                result = self.inputs_status()
                if result["finished"] or not result["active"]:
                    return result
                time.sleep(0.1)
            raise LevelError({"code": "timeout", "message": "Inputs timed out; controls released."})
        except BaseException:
            self.cancel_inputs()
            raise
