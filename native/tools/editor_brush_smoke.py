#!/usr/bin/env python3
"""Live check of terrain brushes through the editor (docs/dev/level-editor/TECH.md 6.3, M5).

One stage smoke on EggStarGalaxy mission 1 with PETARI_EDITOR=1 PETARI_EDITOR_BRUSH_TEST=1
(native/editor/runtime/brush_test.cpp):
1. Through edit_api.hpp: add an EditorBrush row (a checker box with fitted gravity) and Play
   from a marker on it; Mario must stand on it.
2. Resize it, change and undo its material, then delete it.

It must log "[editor-brush] PASS" and the smoke must pass. Screenshots go to
<output>/brush-play.png and brush-resized.png.

usage: editor_brush_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-brush-smoke [--owner editor-terrain]
One app at a time through build/locked-app.sh.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_sweep as sweep  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editor-terrain")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--mode", default="1", help="1: the brush flow; cylinder: a thin rotated cylinder's cap, dropped onto at several radii")
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=1500)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)
    user = args.output / "user"
    shutil.rmtree(user, ignore_errors=True)
    sweep.load_fixture_module().create(args.source, user, "stage")
    command, env_extra = sweep.app_command(args, user, "EggStarGalaxy", 1)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_EDITOR_HOOKS_TEST", "PETARI_EDITOR_TERRAIN_TEST", "PETARI_STAGE_WARP", "PETARI_MOD_FOLDERS"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", PETARI_EDITOR="1", PETARI_EDITOR_BRUSH_TEST=args.mode,
               PETARI_EDITOR_BRUSH_SHOTS=str(args.output.resolve()))
    log = args.output / "brush.log"
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    result = (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1]
    lines = re.findall(r"\[editor-brush\] (.*)", text)
    print(f"brush: exit {status}, smoke {result}")
    for line in lines:
        print(f"  [editor-brush] {line}")
    if not any(line.startswith("PASS") for line in lines) or result != "PASS":
        print("FAIL")
        return 1
    print("PASS: " + ("a brush made through the editor is live, playable, resizable, undoable and deletable" if args.mode == "1"
                      else "Mario lands on the thin rotated cylinder at every radius"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
