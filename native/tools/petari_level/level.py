"""Python API over petari-level (docs/dev/level-editor/AGENT_API.md).

    from petari_level import Level, LevelError

    lv = Level.init("build/ws/coins", disc="build/game-data/RMGE01", galaxy="EggStarGalaxy")
    with lv.batch(key="arc-1", label="Coin arc") as b:
        b.array("Coin", "arc", start=[0, 1200, 0], end=[1500, 1200, 0], peak=600, count=8)
        flight = b.path([[0, 1300, 0], [800, 2600, 0], [1600, 1300, 0]], label="flight")
        b.add("Launch Star", [0, 1200, 0], on_ground=True, height=150, path=flight)
        star = b.add("PowerStar", pos=[1700, 1400, 0], on_ground=True, height=200, args={"Star ID": 1}, label="star")
    print(lv.checks())
    lv.save("Coin Arc")

Every call goes to one long-lived `petari-level serve` process (JSON lines), so the galaxy is
loaded once. Errors raise LevelError with the API's code, hint and candidates.
"""
from __future__ import annotations

import json
import os
import subprocess
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterable, Optional, Sequence

Vec = Sequence[float]


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def find_binary() -> Path:
    """$PETARI_LEVEL, else the build tree's petari-level."""
    env = os.environ.get("PETARI_LEVEL")
    if env:
        return Path(env)
    return _repo_root() / "build/macos-gx/native/editor/level_api/petari-level"


class LevelError(Exception):
    """A failed command: `code` (stable), `message`, `hint`, `candidates`, `where`."""

    def __init__(self, error: dict):
        self.error = error
        self.code = error.get("code", "")
        self.hint = error.get("hint", "")
        self.candidates = error.get("candidates", [])
        self.where = error.get("where", "")
        text = f"{self.code}: {error.get('message', '')}"
        if self.hint:
            text += f" (hint: {self.hint})"
        if self.candidates:
            text += f" [candidates: {', '.join(self.candidates[:6])}]"
        super().__init__(text)


def _clean(args: dict) -> dict:
    return {k: v for k, v in args.items() if v is not None}


def _vectors(kw: dict) -> dict:
    """Tuples (and other sequences) of numbers as JSON lists."""
    return {k: list(v) if isinstance(v, tuple) else v for k, v in kw.items()}


class Batch:
    """Ops collected in a `with lv.batch():` block and applied as ONE transaction (one undo step).

    The builder methods return the ref the new object will have ("@label"), so later ops in the
    same batch can name it. Nothing changes if any op fails."""

    def __init__(self, level: "Level"):
        self.level = level
        self.ops: list[dict] = []
        self._auto = 0

    def _label(self, label: Optional[str]) -> str:
        if label:
            return label.lstrip("@")
        self._auto += 1
        return f"_b{id(self) & 0xffff:x}_{self._auto}"

    def op(self, op: dict) -> "Batch":
        """Any op dict (see `help --op <name>`)."""
        self.ops.append(op)
        return self

    def add(self, cls: str, pos: Optional[Vec] = None, *, label: Optional[str] = None, **kw: Any) -> str:
        """Place one object. kw: zone, layer, missions, like, preset, rot, up, front, turn, scale,
        args, switches, links, fields, on_ground, height, down, align. Returns "@label"."""
        name = self._label(label)
        self.ops.append(_clean({"op": "add", "class": cls, "pos": list(pos) if pos is not None else None, "as": name, **_vectors(kw)}))
        return "@" + name

    def array(self, cls: str, shape: str, *, label: Optional[str] = None, **kw: Any) -> str:
        """Many copies along a shape, with the JSON op's keys: line/arc (start, end, count | spacing, peak),
        ring (center, radius, count), grid (origin, size [nx, nz], spacing), path (points), plus every add
        key (args, on_ground, height, ...). Copies are "@label.0", ".1", ... Returns "@label"."""
        name = self._label(label)
        self.ops.append(_clean({"op": "array", "class": cls, "shape": shape, "as": name, **_vectors(kw)}))
        return "@" + name

    def path(self, points: Sequence[Any], *, label: Optional[str] = None, **kw: Any) -> str:
        """A rail through world points ([x,y,z] or {"pos", "in", "out"}); kw: zone, closed, smooth,
        on_ground, height, down. Returns "@label" (link objects to it with link() or add(path=...))."""
        name = self._label(label)
        pts = [list(p) if not isinstance(p, dict) else p for p in points]
        self.ops.append(_clean({"op": "path", "points": pts, "as": name, **kw}))
        return "@" + name

    def link(self, ref: Any, path: str) -> "Batch":
        """Link an object to a path (its CommonPath_ID); same zone."""
        self.ops.append({"op": "link", "ref": ref, "path": path})
        return self

    def gravity_shape(self, ref: Any, shape: str, *, bound: Optional[str] = None) -> "Batch":
        """Change a field's shape; bound applies to Parallel. Wire requires a linked rail."""
        return self.op(_clean({"op": "gravity-shape", "ref": ref, "shape": shape, "bound": bound}))

    def gravity_values(self, ref: Any, **values: Any) -> "Batch":
        """Core gravity numbers: Range, Distant, Priority, Inverse, Obj_arg0..7."""
        return self.op({"op": "gravity-values", "ref": ref, "values": values})

    def move_point(self, ref: Any, **kw: Any) -> "Batch":
        """Move a rail point and its handles: exactly one of pos, zone_pos or by."""
        return self.op(_clean({"op": "move-point", "ref": ref, **_vectors(kw)}))

    def move_handle(self, ref: Any, handle: str, **kw: Any) -> "Batch":
        """Move the absolute in/out handle: exactly one of pos, zone_pos or by."""
        return self.op(_clean({"op": "move-handle", "ref": ref, "handle": handle, **_vectors(kw)}))

    def insert_point(self, path: Any, segment: int, *, t: float = 0.5, label: Optional[str] = None) -> str:
        """Split a Bezier segment, preserving its curve; return the new point's label."""
        name = self._label(label)
        self.ops.append({"op": "insert-point", "path": path, "segment": segment, "t": t, "as": name})
        return "@" + name

    def delete_point(self, ref: Any) -> "Batch":
        """Delete one point; a rail must retain at least two."""
        return self.op({"op": "delete-point", "ref": ref})

    def along(self, cls: str, path: Any, *, spacing: Optional[float] = None, count: Optional[int] = None,
              label: Optional[str] = None, **kw: Any) -> str:
        """Copies sampled by distance along an existing rail; kw are the add keys."""
        return self.array(cls, "path", label=label, along=path, spacing=spacing, count=count, **kw)

    def set(self, ref: Any, **kw: Any) -> "Batch":
        """Change a row (pos, by, rot, up, args, switches, links, fields, on_ground, ...)."""
        self.ops.append(_clean({"op": "set", "ref": ref, **kw}))
        return self

    def move(self, ref: Any, pos: Optional[Vec] = None, *, by: Optional[Vec] = None, **kw: Any) -> "Batch":
        self.ops.append(_clean({"op": "move", "ref": ref, "pos": list(pos) if pos is not None else None,
                                "by": list(by) if by is not None else None, **kw}))
        return self

    def delete(self, ref: Any = None, *, select: Optional[dict] = None) -> "Batch":
        self.ops.append(_clean({"op": "delete", "ref": ref, "select": select}))
        return self

    def duplicate(self, ref: Any, *, label: Optional[str] = None, **kw: Any) -> str:
        name = self._label(label)
        self.ops.append(_clean({"op": "duplicate", "ref": ref, "as": name, **kw}))
        return "@" + name

    def fix(self, check_key: str) -> "Batch":
        self.ops.append({"op": "fix", "check": check_key})
        return self

    def label(self, ref: Any, name: str) -> str:
        self.ops.append({"op": "label", "ref": ref, "as": name})
        return "@" + name.lstrip("@")


class Level:
    """One workspace (a galaxy + its journal) served by petari-level."""

    def __init__(self, workspace: str | os.PathLike, binary: Optional[str | os.PathLike] = None):
        self.workspace = Path(workspace)
        self.binary = Path(binary) if binary else find_binary()
        if not self.binary.exists():
            raise FileNotFoundError(f"petari-level not found at {self.binary} (build it, or set PETARI_LEVEL_X)")
        self._proc: Optional[subprocess.Popen] = None
        self._next_id = 0
        self.last: dict = {}

    # ---- lifecycle ----------------------------------------------------------------------------

    @classmethod
    def init(cls, workspace: str | os.PathLike, *, disc: str | os.PathLike, galaxy: str, mission: int = 1,
             user: Optional[str | os.PathLike] = None, user_mods: bool = False, force: bool = False,
             binary: Optional[str | os.PathLike] = None) -> "Level":
        """Create a workspace (fails if one exists, unless force) and open it."""
        level = cls(workspace, binary)
        argv = [str(level.binary), "--ws", str(level.workspace), "init", "--disc", str(disc), "--galaxy", galaxy,
                "--mission", str(mission)]
        if user:
            argv += ["--user", str(user)]
        if user_mods:
            argv += ["--user-mods"]
        if force:
            argv += ["--force"]
        reply = json.loads(subprocess.run(argv, capture_output=True, text=True).stdout or "{}")
        if not reply.get("ok"):
            raise LevelError(reply.get("error", {"code": "init", "message": "init failed"}))
        return level

    @classmethod
    def open(cls, workspace: str | os.PathLike, binary: Optional[str | os.PathLike] = None) -> "Level":
        return cls(workspace, binary)

    def _ensure(self) -> subprocess.Popen:
        if self._proc is None or self._proc.poll() is not None:
            self._proc = subprocess.Popen([str(self.binary), "--ws", str(self.workspace), "serve"], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, text=True, bufsize=1)
        return self._proc

    def close(self) -> None:
        if self._proc and self._proc.poll() is None:
            try:
                self._proc.stdin.write('{"cmd": "quit"}\n')
                self._proc.stdin.flush()
                self._proc.wait(timeout=10)
            except Exception:
                self._proc.kill()
        self._proc = None

    def __enter__(self) -> "Level":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    # ---- the one primitive -------------------------------------------------------------------

    def call(self, cmd: str, **args: Any) -> dict:
        """Run a command; returns its reply (without "ok"); raises LevelError."""
        proc = self._ensure()
        self._next_id += 1
        proc.stdin.write(json.dumps({"id": self._next_id, "cmd": cmd, "args": _clean(args)}) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        if not line:
            # The workspace could not be opened: the server printed its error and exited.
            raise LevelError({"code": "server", "message": f"petari-level serve exited ({proc.poll()})",
                              "hint": f"run: {self.binary} --ws {self.workspace} info"})
        reply = json.loads(line)
        if not reply.get("ok"):
            if reply.get("error", {}).get("code") in (None, ""):
                raise LevelError({"code": "server", "message": line.strip()})
            raise LevelError(reply["error"])
        self.last = reply
        reply.pop("ok", None)
        reply.pop("id", None)
        return reply

    # ---- discovery --------------------------------------------------------------------------------

    def help(self, command: Optional[str] = None, op: Optional[str] = None) -> dict:
        return self.call("help", command=command, op=op)

    def info(self, *, existing_checks: bool = False) -> dict:
        return self.call("info", existing_checks=existing_checks or None)

    def catalog(self, query: Optional[str] = None, *, category: Optional[str] = None, placed_here: Optional[bool] = None,
                limit: Optional[int] = None) -> list[dict]:
        return self.call("catalog", query=query, category=category, placed_here=placed_here, limit=limit)["classes"]

    def describe(self, cls: str) -> dict:
        """A class in full: args by name, enum choices, switches, links, presets."""
        return self.call("class", **{"class": cls})

    def list(self, cls: Optional[str] = None, *, full: bool = False, limit: Optional[int] = None, **filters: Any) -> list[dict]:
        """Objects: filters class, zone, category, mission, table, state, text, near, radius."""
        if cls:
            filters["class"] = cls
        return self.call("list", full=full or None, limit=limit, **filters)["objects"]

    def get(self, ref: Any) -> dict:
        return self.call("get", ref=ref)

    # ---- edits ------------------------------------------------------------------------------------

    def apply(self, ops: Iterable[dict], *, key: Optional[str] = None, label: Optional[str] = None,
              dry_run: bool = False, existing_checks: bool = False) -> dict:
        """One transaction. Returns created/changed/deleted, placements and the checks introduced."""
        return self.call("apply", ops=list(ops), key=key, label=label, dry_run=dry_run or None, existing_checks=existing_checks or None)

    @contextmanager
    def batch(self, *, key: Optional[str] = None, label: Optional[str] = None, dry_run: bool = False, existing_checks: bool = False):
        """`with lv.batch(key=...) as b:` ... applied at the end of the block (nothing if it raises)."""
        b = Batch(self)
        yield b
        if b.ops:
            b.result = self.apply(b.ops, key=key, label=label, dry_run=dry_run, existing_checks=existing_checks)

    def add(self, cls: str, pos: Optional[Vec] = None, **kw: Any) -> str:
        """Add one object in its own batch; returns its ref."""
        with self.batch() as b:
            ref = b.add(cls, pos, **kw)
        created = b.result.get("created", [])
        return created[0]["ref"] if created else ref

    def set(self, ref: Any, **kw: Any) -> dict:
        return self.apply([_clean({"op": "set", "ref": ref, **kw})])

    def delete(self, ref: Any) -> dict:
        return self.apply([{"op": "delete", "ref": ref}])

    def undo(self, steps: int = 1) -> dict:
        return self.call("undo", steps=steps)

    def redo(self, steps: int = 1) -> dict:
        return self.call("redo", steps=steps)

    def history(self) -> dict:
        return self.call("history")

    # ---- verification -----------------------------------------------------------------------------

    def checks(self, *, all: bool = False) -> dict:
        return self.call("checks", all=all or None)

    def diff(self) -> list[dict]:
        return self.call("diff")["rows"]

    def ground(self, pos: Vec, *, down: Optional[Vec] = None) -> dict:
        return self.call("ground", pos=list(pos), down=list(down) if down else None)

    def gravity(self, pos: Vec) -> dict:
        return self.call("gravity", pos=list(pos))

    def colliders(self) -> dict:
        return self.call("colliders")

    # ---- output -------------------------------------------------------------------------------------

    def save(self, mod: str, *, full: bool = False) -> dict:
        """Write <user>/mods/<mod> (a patch unless full) and turn it on for the next start."""
        return self.call("save", mod=mod, full=full or None)

    def export(self, start: int = 0, stop: Optional[int] = None) -> list[dict]:
        """The journal's low-level ops (what live mode sends)."""
        return self.call("export", **{"from": start, "to": stop})["ops"]
